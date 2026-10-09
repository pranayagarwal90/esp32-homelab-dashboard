# ESP32 Homelab Dashboard

A touchscreen dashboard built on the ESP32-2432S028R (Cheap Yellow Display / CYD) for monitoring a homelab and Windows host from a desk.

The display is designed for quick-glance information rather than deep administration. It currently shows:

- Windows CPU usage
- Windows RAM usage
- Disk usage for C:, D:, and E:
- GPU utilization
- Wi-Fi link speed and signal strength
- Network RX/TX throughput
- Windows uptime
- Homelab service health
- Local and international time
- Weather
- Monthly calendar
- Photos/slideshow
- Screensaver
- Tic-Tac-Toe
- Reaction game
- OTA firmware updates

---

# 1. High-level architecture

The project has three main components:

```text
┌─────────────────────────────────────┐
│ Windows HomeServer                  │
│                                     │
│ Host Metrics Agent                  │
│ Port 9183                           │
│                                     │
│ Provides:                           │
│ - CPU                               │
│ - RAM                               │
│ - Disks                             │
│ - GPU                               │
│ - Wi-Fi / Ethernet                  │
│ - Uptime                            │
└─────────────────┬───────────────────┘
                  │
                  │ HTTP
                  ▼
┌─────────────────────────────────────┐
│ WSL Ubuntu                          │
│                                     │
│ Homelab Dashboard API               │
│ Port 8090                           │
│                                     │
│ Combines:                           │
│ - Windows host metrics              │
│ - Homelab services                  │
│ - Docker information                │
│ - Weather                           │
│ - Time zones                        │
│ - Photos                            │
└─────────────────┬───────────────────┘
                  │
                  │ HTTP
                  ▼
┌─────────────────────────────────────┐
│ ESP32 CYD                           │
│                                     │
│ Touchscreen dashboard               │
│                                     │
│ Displays all data                   │
│ and provides touch navigation       │
└─────────────────────────────────────┘
```

# Service health checks

The Services tab displays Jellyfin, Navidrome, Ollama, Cloudfare, NextCloud,
Immich, and Technical Blog in that order. The `/api/status` services object
retains its original seven boolean keys for older firmware and adds `nextcloud`,
`immich`, and `technical_blog`. New firmware defaults missing keys to false.

## Verified deployment defaults (2026-10-02)

These URLs were verified against local deployment configuration and read-only
HTTP probes. They are specific to this homelab. Each returned HTTP 200.

| Service | Default URL | Check / source |
| --- | --- | --- |
| Jellyfin | `http://192.168.1.13:8096/health` | 2xx; Windows `network.xml` port and [official health endpoint](https://jellyfin.org/docs/general/post-install/networking/advanced/monitoring/) |
| Navidrome | `http://127.0.0.1:4533/ping` | 2xx; published port and installed Docker healthcheck |
| Ollama | `http://127.0.0.1:11434/api/version` | 2xx plus nonempty version string; published port and [official API](https://github.com/ollama/ollama/blob/main/docs/api.md#version) |
| NextCloud | `http://127.0.0.1:8080/status.php` | 2xx plus installed=true, maintenance=false, needsDbUpgrade=false; installed Docker healthcheck and observed JSON |
| Immich | `http://127.0.0.1:2283/api/server/ping` | 2xx plus res=pong; published port and installed `immich-healthcheck` script |
| Technical Blog | `http://192.168.1.13:8085/` | 2xx; live deployment runbook and Docker healthcheck; page availability, not exhaustive dependency health |
| Cloudflare | None | Existing Docker fallback; tunnel has no published readiness port |

The verified Compose files are under `/mnt/c/homeserver/compose/`.
Jellyfin's port is recorded in
`/mnt/c/ProgramData/Jellyfin/Server/config/network.xml`. Technical Blog's
live URL is documented in the adjacent repository's
`docs/live-deployment-runbook.md`. No external configurations were changed.
Loopback URLs assume the API runs on the same WSL host as these published ports.

Override a default in the backend process environment using
`SERVICE_<NAME>_HEALTH_URL`, with uppercase names: `JELLYFIN`, `NAVIDROME`,
`OLLAMA`, `NEXTCLOUD`, `IMMICH`, `TECHNICAL_BLOG`, or `CLOUDFLARE`.
Legacy-only services support `SERVICE_METUBE_HEALTH_URL`,
`SERVICE_BAZARR_HEALTH_URL`, and `SERVICE_MCP_HEALTH_URL`; they have no defaults
and retain existing Docker detection when unconfigured. An explicitly blank
variable disables its default URL and selects fallback. The new entries have
no assumed container identities, so their fallback is false.

Configured checks issue HTTP(S) GET without redirects or proxy environment
variables. HTTPS verifies certificates. Custom authentication headers and URLs
with embedded credentials are unsupported. A failed configured check returns
false even if its container is running. JSON health responses are limited to
4096 bytes; application-page bodies are not downloaded. Malformed JSON,
missing fields, and unexpected payloads return false.

Checks run concurrently with 0.5-second connection/read socket timeouts.
This is a per-operation timeout, not a strict total deadline: DNS resolution
and slowly delivered headers can take longer. The verified defaults use IP
addresses. Existing metrics, Docker, and weather timeouts are unchanged.
Cloudflare's container fallback proves only that its process is running,
not that the tunnel is connected.

## Backend startup correction for review

The installed `/etc/systemd/system/homelab-dashboard.service` points to a
missing root-level `server.py`. The prepared
`backend/homelab-dashboard.service.d/override.conf` corrects `ExecStart` to
`backend/server.py`. The user installed it and restarted the API on 2026-10-02; the
active service now executes `backend/server.py`. The `backend/photos-ready` link points to the existing root-level photo
directory, preserving its contents and URL behavior. No photo logic was changed.
Review the existing runtime configuration before applying this override. Backend rollout is separate from
firmware upload; `deploy-esp32.sh` is unchanged.

Run isolated tests without contacting production:

```sh
python3 -m py_compile backend/server.py backend/test_service_health.py
python3 -m unittest discover -s backend -p 'test_service_health.py' -v
```

Deployment verification on 2026-10-02: the restarted API returned HTTP 200,
all seven displayed services true, Windows metrics available, and 20 photos.
An individual photo returned HTTP 200 with image/jpeg content type. The existing
`./deploy-esp32.sh` workflow built both firmware copies and completed OTA to
`192.168.1.18` with device result OK. The deployment script and Windows secrets
file were unchanged. Physical screen rendering still requires a visual check.

## Automatic backlight brightness

The backend requests sunrise/sunset with the existing Open-Meteo weather
request and 15-minute cache. It requests two forecast days to cover the night
across midnight; a new local date refreshes the cache. Existing weather fields
are preserved. Optional `weather` fields are integer UTC epoch seconds:

- `sunrise_timestamp`, `sunset_timestamp`: today's solar transitions.
- `solar_day_start`, `solar_day_end`: local midnight boundaries, using
  `America/New_York` and its DST rules.
- `solar_valid_until`: tomorrow's sunrise, or today's ending midnight if
  tomorrow's sunrise is unavailable. Solar fields are omitted if today's
  solar values are invalid; weather remains available independently.

Firmware compares these values against the status `timestamp`, advancing it
with `millis()` between responses. GPIO 21 uses active-high LEDC channel 0,
5 kHz, 8-bit PWM: day 100% (255), night 15% (38), touch boost 60% (153).
An accepted night touch restarts a nonblocking 30-second boost. Day touches
leave full brightness unchanged. Invalid or expired solar data and PWM setup
failure use full brightness. PWM duty is written only when it changes, and
TFT_eSPI no longer independently drives the backlight during initialization.
Startup uses full brightness until the first valid status is applied.

Run the isolated checks and build without uploading:

```sh
python3 -m py_compile backend/server.py backend/test_weather.py
python3 -m unittest discover -s backend -p 'test_*.py' -v
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_backlight.cpp -o /tmp/test-backlight
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_photo_requests.cpp -o /tmp/test-photo-requests
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_settings.cpp -o /tmp/test-settings
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_alerts.cpp -o /tmp/test-alerts
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_stopwatch.cpp -o /tmp/test-stopwatch
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_games.cpp -o /tmp/test-games
/tmp/test-ota-animation
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_ota_animation.cpp -o /tmp/test-ota-animation
/tmp/test-settings
/tmp/test-alerts
/tmp/test-stopwatch
/tmp/test-games
/tmp/test-ota-animation
/tmp/test-photo-requests
/tmp/test-backlight
pio run -d firmware -e esp32dev
git diff --check
```

## Firmware layout

`firmware/src/main.cpp` only wires up `setup()` and `loop()`. Each screen or
subsystem has a header in `firmware/include/` and a source in `firmware/src/`:

| Module | Responsibility |
|---|---|
| `AppState` | Page enum and the main-task copy of status data (`app`) |
| `PageRouter` | `drawCurrentPage()` / `showPage()`; the only file that knows every screen |
| `UiHelpers`, `Display` | Shared header/nav/back bars; the `tft` instance |
| `HomeScreen`, `ServicesScreen`, `MenuScreens`, `TimeWeatherScreen`, `CalendarScreen`, `PhotoScreen`, `Screensaver` | One screen each: drawing and its touch zones |
| `games/TicTacToe`, `games/ReactionGame`, `games/SnakeGame`, `games/MemoryGame`, `games/SimonGame` | Game state, drawing, touch, timing |
| `games/SnakeLogic.h`, `games/MemoryLogic.h`, `games/SimonLogic.h`, `games/GameRandom.h` | Pure, host-tested game rules and millis()-driven state machines |
| `StopwatchScreen`, `Stopwatch.h` | MORE > TOOLS > STOPWATCH page and its pure, host-tested timing |
| `MenuLayout.h` | Pure MORE / GAMES / TOOLS layout, hit-testing and BACK targets |
| `TouchHandler` | XPT2046 read, calibration, debounce, wake, backlight boost, dispatch |
| `StatusClient` | FreeRTOS `/api/status` worker, snapshot hand-off, refresh timing |
| `PhotoClient` | FreeRTOS worker for `/api/photos` and JPEG downloads; hands the JPEG buffer to the main task |
| `SettingsScreen`, `DisplaySettingsScreen`, `WifiSettingsScreen` | MORE > SETTINGS menu and its sub-pages |
| `DeviceSettings.h`, `SettingsLogic.h` | Pure, host-tested settings model, NVS encoding, navigation and formatting |
| `SettingsStore` | NVS persistence (debounced, change-only writes) and the saved Wi-Fi network |
| `WifiProvisioning` | Temporary setup hotspot and web form, served from its own task |
| `PowerManager`, `BluetoothControl` | Restart, deep sleep, boot-time Bluetooth controller |
| `AlertLogic.h`, `AlertManager`, `AlertsScreen` | Pure, host-tested alert rules; evaluation on each status result; MORE > ALERTS page and the HOME indicator |
| `PhotoRequestTracker.h` | Pure, host-tested request generations: stale or cancelled photo results are discarded |
| `NetworkManager`, `OtaManager`, `BacklightPwm` | Wi-Fi connect/reconnect, ArduinoOTA, LEDC driver |
| `OtaAnimation`, `OtaAnimationLogic.h` | Walking-man OTA progress screen; pure, host-tested frames, progress mapping and timing |

TFT drawing and JPEG decoding happen only on the Arduino loop task; the
status and photo workers only do network I/O and never draw.
`deploy-esp32.sh` mirrors all of `firmware/src/` (deleting stale files) and
copies `firmware/include/` to the Windows project, never copying or deleting
`secrets.h`.

## Alerts

The dashboard evaluates active alerts on the device whenever a status result
arrives (no extra task, no backend changes). Alerts are active conditions
only: they clear by themselves, with no acknowledgement or history.

| Alert | Severity | Rule |
|---|---|---|
| Jellyfin, Navidrome, MeTube, Ollama, Cloudflare offline | WARNING | Health key reported `false`; a missing key is unknown, never an outage |
| RAM, each disk (C:, D:, E: when reported) | WARNING / CRITICAL | Warning from 85 % until below 82 %; critical from 95 % until below 92 %, then warning |
| Status data stale | WARNING | At least 3 consecutive failed fetches and 60 s since the last success |

When `/api/status` reports `host_available: false` (cached or zero host
metrics) RAM and disk alerts keep their previous state. A disk missing from a
valid report clears its alert. Time on the Games and Photos pages, which skip
status refreshes, never counts toward staleness. HOME shows `ALL GOOD`,
`2 WARNINGS` or `1 CRITICAL +2` on the RAM line; tap it, or MORE > ALERTS, to
see the list (three per page, PREV / NEXT). Thresholds live in
`firmware/include/AlertLogic.h`.

## Tools and games

MORE has four rows: TIME / WEATHER, CALENDAR, GAMES, PHOTOS, ALERTS,
SETTINGS and a full-width TOOLS. BACK goes tool > TOOLS > MORE and
game > GAMES > MORE.

- **TOOLS > STOPWATCH**: start / pause / resume, reset and up to 10 laps
  (further laps are refused). It keeps running on other pages and under the
  screensaver; only RESET stops and clears it. The digits refresh every
  100 ms without redrawing the page.
- **GAMES**: Tic-Tac-Toe, Reaction Tap, Snake, Memory Match and Simon Says.
  Snake uses an on-screen direction pad (tap the board to pause), Memory Match
  is a 4x4 grid of numbered pairs, and Simon Says has four large pads.

Everything is local and timed with millis() state machines (no delay(), no
extra tasks). Games pause when their page is left, including for the
screensaver: Snake resumes on an arrow tap, Memory Match turns a pending
mismatched pair back over, and Simon replays an interrupted round from its
first step. Game state stays until RESTART. As before, game pages skip status
fetches (that time does not count toward the stale-data alert); the stopwatch
keeps normal fetching but is never repainted by status updates.

## OTA update screen

ArduinoOTA receives the whole upload inside `handleOTA()`, so the dashboard
is paused meanwhile and the OTA screen owns the display. A six-frame
stick figure walks toward a house as the upload progresses (about 7 frames
per second, stepped from the progress callback), above a progress bar,
the percentage and "Do not power off". At 100 % he stands at the house and
the screen shows UPDATE COMPLETE / Restarting... just before the reboot. A
failed upload shows the error, how far it got and that the current firmware
is still installed, for 10 s, then returns to the previous page. At night the
normal touch boost brightens the screen; no setting is changed.

## Settings

MORE > SETTINGS is a three-level list (BACK always goes one level up):

- **SYSTEM**: Device Info, Firmware / Build, Restart, Sleep
- **CONNECTIVITY**: Wi-Fi, Bluetooth
- **DISPLAY**: Brightness, Screensaver, Photos & Wallpaper

Settings Settings are stored in NVS (namespace `settings`) and
defaults reproduce the previous fixed behaviour (auto brightness 100/15/60%,
3 min screensaver, 30 s photo/clock rotation, Bluetooth off).

- **Wi-Fi > Change Wi-Fi** opens a hotspot `Homelab-Setup-XXXX` whose random
  password is shown only on the display. Join it, open `http://192.168.4.1`
  and submit the network. The dashboard stays online meanwhile; if the new
  network fails within 20 s the previous one is restored. The saved network
  is stored in NVS (namespace `wifi`, not encrypted) and `secrets.h` remains
  the fallback. **Use built-in** forgets the saved network.
- **Bluetooth** is not compiled in by default (the controller library costs
  ~149 KB flash and ~5 KB RAM even when off); the page then says so. To
  enable it, add `-D DASHBOARD_BLUETOOTH=1` to `build_flags` in
  `firmware/platformio.ini`. With it, Bluetooth is a boot-time preference
  (default off): the core frees Bluetooth memory at boot when it is off, so
  changes apply after a restart. Only the controller is started; there are no
  Bluetooth services yet.
- **Sleep** is deep sleep, not power-off (the board has no power switch
  control). RST always wakes it; touch wake (GPIO36) is attempted but not yet
  verified on this board: "Wake: tap screen if supported, or press RST."
- **Wallpaper** selects the photo the screensaver shows, or rotates all photos.

### Firmware version and build info

The release version lives only in `firmware/include/BuildInfo.h`
(`DASHBOARD_FIRMWARE_VERSION`, semantic `MAJOR.MINOR.PATCH`, currently
`1.0.0`); bump it by hand for each release. SYSTEM > Firmware / Build also
shows the build time (compiler `__DATE__`/`__TIME__`), the short git commit
(`+dirty` for uncommitted changes), the PlatformIO environment and board.
`firmware/scripts/build_info.py` (a PlatformIO `extra_scripts` hook) injects
the commit and environment into `src/BuildInfo.cpp` only and rebuilds that file
every time so the timestamp is current. Git is optional: outside a checkout the
commit shows `unknown`, and `DASHBOARD_GIT_SHA` in the environment overrides
it. `deploy-esp32.sh` syncs `firmware/scripts/` and passes the commit to the
Windows build, which is not a git checkout.
