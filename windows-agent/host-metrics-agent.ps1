param(
    [switch]$Install,
    [int]$Port = 9183
)

$ErrorActionPreference = 'Stop'
$taskName = 'HomeServer Host Metrics Agent'
$firewallName = 'HomeServer Host Metrics Agent'
$legacyTaskName = 'HomeServer GPU Metrics Agent'
$legacyFirewallName = 'HomeServer GPU Metrics Agent'
$installDirectory = Join-Path $env:ProgramData 'HomeServerHostMetrics'
$installedScript = Join-Path $installDirectory 'host-metrics-agent.ps1'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Install-Agent {
    if (-not (Test-Administrator)) {
        throw 'Run PowerShell as Administrator to install the host metrics agent.'
    }

    New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
    Copy-Item -LiteralPath $PSCommandPath -Destination $installedScript -Force

    Get-ScheduledTask -TaskName $legacyTaskName -ErrorAction SilentlyContinue |
        Stop-ScheduledTask -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $legacyTaskName -Confirm:$false -ErrorAction SilentlyContinue
    Get-NetFirewallRule -DisplayName $legacyFirewallName -ErrorAction SilentlyContinue |
        Remove-NetFirewallRule

    $action = New-ScheduledTaskAction `
        -Execute 'powershell.exe' `
        -Argument "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$installedScript`" -Port $Port"
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $settings = New-ScheduledTaskSettingsSet `
        -ExecutionTimeLimit ([TimeSpan]::Zero) `
        -RestartCount 5 `
        -RestartInterval (New-TimeSpan -Minutes 1) `
        -StartWhenAvailable

    Register-ScheduledTask `
        -TaskName $taskName `
        -Action $action `
        -Trigger $trigger `
        -Settings $settings `
        -User 'SYSTEM' `
        -RunLevel Highest `
        -Force | Out-Null

    Get-NetFirewallRule -DisplayName $firewallName -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    New-NetFirewallRule `
        -DisplayName $firewallName `
        -Direction Inbound `
        -Protocol TCP `
        -LocalPort $Port `
        -RemoteAddress LocalSubnet,100.64.0.0/10,172.16.0.0/12 `
        -Action Allow | Out-Null

    Start-ScheduledTask -TaskName $taskName
    Write-Host "Host metrics agent installed on port $Port."
    Write-Host "Endpoint: http://localhost:$Port/metrics"
}

function Find-NvidiaSmi {
    $command = Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $standardPath = Join-Path $env:ProgramFiles 'NVIDIA Corporation\NVSMI\nvidia-smi.exe'
    if (Test-Path -LiteralPath $standardPath) { return $standardPath }
    return $null
}

function Get-NvidiaMetrics {
    $nvidiaSmi = Find-NvidiaSmi
    if (-not $nvidiaSmi) { return @() }

    $fields = 'name,utilization.gpu,memory.used,memory.total,temperature.gpu,utilization.encoder,utilization.decoder,power.draw'
    $lines = & $nvidiaSmi --query-gpu=$fields --format=csv,noheader,nounits 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $lines) { return @() }

    return @($lines | ConvertFrom-Csv -Header Name,UsagePercent,MemoryUsedMiB,MemoryTotalMiB,TemperatureC,EncoderPercent,DecoderPercent,PowerWatts | ForEach-Object {
        [ordered]@{
            name = $_.Name.Trim()
            vendor = 'NVIDIA'
            usagePercent = [double]$_.UsagePercent
            memoryUsedBytes = [double]$_.MemoryUsedMiB * 1MB
            memoryTotalBytes = [double]$_.MemoryTotalMiB * 1MB
            temperatureC = [double]$_.TemperatureC
            encoderPercent = [double]$_.EncoderPercent
            decoderPercent = [double]$_.DecoderPercent
            powerWatts = if ($_.PowerWatts -match '^\s*N/A') { $null } else { [double]$_.PowerWatts }
        }
    })
}

function Get-WindowsGpuSnapshot {
    $result = @{ usage = $null; dedicated = $null; shared = $null }
    try {
        $paths = @(
            '\GPU Engine(*)\Utilization Percentage',
            '\GPU Adapter Memory(*)\Dedicated Usage',
            '\GPU Adapter Memory(*)\Shared Usage'
        )
        $samples = (Get-Counter $paths -ErrorAction Stop).CounterSamples
        $byEngine = @{}
        foreach ($sample in $samples) {
            if ($sample.Path -like '*GPU Engine*Utilization Percentage') {
                if ($sample.InstanceName -notmatch 'engtype_([^_]+)') { continue }
                $engine = $Matches[1]
                if (-not $byEngine.ContainsKey($engine)) { $byEngine[$engine] = 0.0 }
                $byEngine[$engine] += [double]$sample.CookedValue
            } elseif ($sample.Path -like '*GPU Adapter Memory*Dedicated Usage') {
                if ($null -eq $result.dedicated) { $result.dedicated = 0.0 }
                $result.dedicated += [double]$sample.CookedValue
            } elseif ($sample.Path -like '*GPU Adapter Memory*Shared Usage') {
                if ($null -eq $result.shared) { $result.shared = 0.0 }
                $result.shared += [double]$sample.CookedValue
            }
        }
        if ($byEngine.Count) {
            $result.usage = [Math]::Min(100, ($byEngine.Values | Measure-Object -Maximum).Maximum)
        }
    } catch { }
    return $result
}

function Get-GenericGpuMetrics {
    $controllers = @(Get-CimInstance Win32_VideoController | Where-Object { $_.Name -notmatch 'Remote Display|Basic Display' })
    if (-not $controllers.Count) { return @() }

    $snapshot = Get-WindowsGpuSnapshot
    return @($controllers | ForEach-Object {
        [ordered]@{
            name = $_.Name
            vendor = if ($_.Name -match 'Intel') { 'Intel' } elseif ($_.Name -match 'AMD|Radeon') { 'AMD' } else { 'Windows' }
            usagePercent = $snapshot.usage
            memoryUsedBytes = if ($snapshot.dedicated) { $snapshot.dedicated } else { $snapshot.shared }
            memoryTotalBytes = if ($_.AdapterRAM) { [double]$_.AdapterRAM } else { $null }
            temperatureC = $null
            encoderPercent = $null
            decoderPercent = $null
            powerWatts = $null
        }
    })
}

function Get-WifiSignalPercent {
    try {
        $lines = netsh wlan show interfaces 2>$null
        foreach ($line in $lines) {
            if ($line -match '^\s*Signal\s*:\s*(\d+)%') {
                return [int]$Matches[1]
            }
        }
    } catch { }
    return $null
}

function Get-SystemMetrics {
    $operatingSystem = Get-CimInstance Win32_OperatingSystem
    $processor = Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'"

    $totalMemory = [double]$operatingSystem.TotalVisibleMemorySize * 1KB
    $freeMemory = [double]$operatingSystem.FreePhysicalMemory * 1KB
    $usedMemory = [Math]::Max(0.0, $totalMemory - $freeMemory)

    $lastBoot = [datetime]$operatingSystem.LastBootUpTime
    $uptimeSeconds = [Math]::Max(0.0, ([DateTime]::Now - $lastBoot).TotalSeconds)

    $disks = @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' | Sort-Object DeviceID | ForEach-Object {
        $size = [double]$_.Size
        $free = [double]$_.FreeSpace
        [ordered]@{
            name = $_.DeviceID
            label = $_.VolumeName
            totalBytes = $size
            usedBytes = [Math]::Max(0.0, $size - $free)
            freeBytes = $free
            usagePercent = if ($size -gt 0) { [Math]::Round((($size - $free) / $size) * 100, 1) } else { $null }
        }
    })

    return [ordered]@{
        cpu = [ordered]@{
            usagePercent = if ($processor) { [double]$processor.PercentProcessorTime } else { $null }
        }
        memory = [ordered]@{
            totalBytes = $totalMemory
            usedBytes = $usedMemory
            freeBytes = $freeMemory
            usagePercent = if ($totalMemory -gt 0) { [Math]::Round(($usedMemory / $totalMemory) * 100, 1) } else { $null }
        }
        disks = $disks
        uptimeSeconds = [Math]::Round($uptimeSeconds, 0)
    }
}

function Get-NetworkMetrics {
    $adapters = @(NetAdapter\Get-NetAdapter -Physical -ErrorAction SilentlyContinue)
    if (-not $adapters.Count) { return @() }

    $wifiSignal = Get-WifiSignalPercent

    $before = @{}
    foreach ($stat in @(NetAdapter\Get-NetAdapterStatistics -ErrorAction SilentlyContinue)) {
        $before[$stat.Name] = $stat
    }

    $sampleStarted = [DateTime]::UtcNow
    Start-Sleep -Milliseconds 500
    $sampleSeconds = [Math]::Max(0.1, ([DateTime]::UtcNow - $sampleStarted).TotalSeconds)

    $after = @{}
    foreach ($stat in @(NetAdapter\Get-NetAdapterStatistics -ErrorAction SilentlyContinue)) {
        $after[$stat.Name] = $stat
    }

    return @($adapters | Sort-Object Name | ForEach-Object {
        $adapter = $_
        $first = $before[$adapter.Name]
        $last = $after[$adapter.Name]

        $receivedBytesPerSecond = if ($first -and $last) {
            [Math]::Max(0.0, ([double]$last.ReceivedBytes - [double]$first.ReceivedBytes) / $sampleSeconds)
        } else { 0 }

        $sentBytesPerSecond = if ($first -and $last) {
            [Math]::Max(0.0, ([double]$last.SentBytes - [double]$first.SentBytes) / $sampleSeconds)
        } else { 0 }

        $linkBitsPerSecond = [Math]::Max([double]$adapter.ReceiveLinkSpeed, [double]$adapter.TransmitLinkSpeed)
        $usage = if ($linkBitsPerSecond -gt 0) {
            [Math]::Min(100, (($receivedBytesPerSecond + $sentBytesPerSecond) * 8 / $linkBitsPerSecond) * 100)
        } else { 0 }

        $isWifi = $adapter.Name -match 'Wi-?Fi|Wireless' -or $adapter.InterfaceDescription -match 'Wi-?Fi|Wireless|802\.11'

        [ordered]@{
            name = $adapter.Name
            description = $adapter.InterfaceDescription
            type = if ($isWifi) { 'wifi' } else { 'ethernet' }
            status = [string]$adapter.Status
            linkMbps = [Math]::Round($linkBitsPerSecond / 1000000, 1)
            receiveMbps = [Math]::Round(($receivedBytesPerSecond * 8) / 1000000, 2)
            sendMbps = [Math]::Round(($sentBytesPerSecond * 8) / 1000000, 2)
            usagePercent = [Math]::Round($usage, 1)
            signalPercent = if ($isWifi) { $wifiSignal } else { $null }
        }
    })
}

function Get-Metrics {
    $gpus = @(Get-NvidiaMetrics)
    if (-not $gpus.Count) { $gpus = @(Get-GenericGpuMetrics) }

    $system = Get-SystemMetrics

    return [ordered]@{
        hostname = $env:COMPUTERNAME
        timestamp = [DateTime]::UtcNow.ToString('o')
        uptimeSeconds = $system.uptimeSeconds
        gpus = $gpus
        cpu = $system.cpu
        memory = $system.memory
        disks = $system.disks
        networks = @(Get-NetworkMetrics)
    }
}

if ($Install) {
    Install-Agent
    exit 0
}

$listener = [Net.HttpListener]::new()
$listener.Prefixes.Add("http://+:$Port/")
$listener.Start()
Write-Host "Host metrics agent listening on port $Port"

$cachedJson = $null
$cacheTime = [DateTime]::MinValue

while ($listener.IsListening) {
    $context = $listener.GetContext()
    try {
        if ($context.Request.HttpMethod -ne 'GET' -or $context.Request.Url.AbsolutePath -notin @('/', '/metrics', '/health')) {
            $context.Response.StatusCode = 404
        } else {
            if (-not $cachedJson -or ([DateTime]::UtcNow - $cacheTime).TotalSeconds -ge 2) {
                $cachedJson = Get-Metrics | ConvertTo-Json -Depth 5 -Compress
                $cacheTime = [DateTime]::UtcNow
            }

            $bytes = [Text.Encoding]::UTF8.GetBytes($cachedJson)
            $context.Response.StatusCode = 200
            $context.Response.ContentType = 'application/json; charset=utf-8'
            $context.Response.Headers['Access-Control-Allow-Origin'] = '*'
            $context.Response.Headers['Cache-Control'] = 'no-store'
            $context.Response.ContentLength64 = $bytes.Length
            $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
        }
    } catch {
        $errorLine = "[$([DateTime]::UtcNow.ToString('o'))] $($_.Exception.ToString())"
        Add-Content -LiteralPath (Join-Path $installDirectory 'error.log') -Value $errorLine -ErrorAction SilentlyContinue
        $context.Response.StatusCode = 500
    } finally {
        $context.Response.Close()
    }
}
