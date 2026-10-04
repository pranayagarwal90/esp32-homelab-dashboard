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
| `games/TicTacToe`, `games/ReactionGame` | Game state, drawing, touch, timing |
| `TouchHandler` | XPT2046 read, calibration, debounce, wake, backlight boost, dispatch |
| `StatusClient` | FreeRTOS `/api/status` worker, snapshot hand-off, refresh timing |
| `PhotoClient` | FreeRTOS worker for `/api/photos` and JPEG downloads; hands the JPEG buffer to the main task |
| `PhotoRequestTracker.h` | Pure, host-tested request generations: stale or cancelled photo results are discarded |
| `NetworkManager`, `OtaManager`, `BacklightPwm` | Wi-Fi connect/reconnect, ArduinoOTA, LEDC driver |

TFT drawing and JPEG decoding happen only on the Arduino loop task; the
status and photo workers only do network I/O and never draw.
`deploy-esp32.sh` mirrors all of `firmware/src/` (deleting stale files) and
copies `firmware/include/` to the Windows project, never copying or deleting
`secrets.h`.
