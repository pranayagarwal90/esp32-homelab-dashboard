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

# Backend configuration

The backend has no deployment-specific values in its source. Everything that
depends on your network, location or machine is read by `backend/config.py`:

```sh
cp .env.example .env    # git-ignored; edit your values here
python3 backend/server.py
```

- Priority: process environment (e.g. systemd `Environment=`) > `.env` in the
  project root (`DASHBOARD_ENV_FILE` selects another file; blank = none) >
  generic defaults.
- Blank core settings (port, timezone, ...) use the default; blank optional
  integrations (weather, host metrics, service URLs, primary AI) are disabled.
- An invalid core setting (port, timezone, world clock, boolean) stops startup
  with `Configuration error: ...`. An invalid optional integration (for
  example a latitude outside -90..90, or only one coordinate) is disabled and
  printed as a warning at startup.
- `DASHBOARD_PHOTOS_DIR` may be absolute; a relative path is resolved from the
  project root (default `./photos-ready`), never from the working directory.
  The directory is created when missing.
- `DASHBOARD_WORLD_CLOCKS` (`Label=Area/City,...`) adds clocks to
  `/api/status`. The current firmware still shows only the `india`,
  `singapore` and `london` keys; unconfigured ones are sent as `--:--`.
  Fully configurable clocks need a later firmware/protocol change.

See `.env.example` for every setting.

# Service health checks

The Services tab displays Jellyfin, Navidrome, Ollama, Cloudfare, NextCloud,
Immich, and Technical Blog in that order. The `/api/status` services object
retains its original seven keys for older firmware and adds `nextcloud`,
`immich`, and `technical_blog`. Each value is `true` (up), `false` (down) or
`null` (no way to check it); the firmware shows `null` or a missing key as
unknown (grey) and never raises an alert for it.

## Health URLs

There are no source defaults. Configure a check with
`SERVICE_<NAME>_HEALTH_URL`, with uppercase names: `JELLYFIN`, `NAVIDROME`,
`METUBE`, `OLLAMA`, `NEXTCLOUD`, `IMMICH`, `TECHNICAL_BLOG`, `CLOUDFLARE`,
`BAZARR` or `MCP`. Service-specific checks:

| Service | Typical URL | Healthy when |
| --- | --- | --- |
| Jellyfin | `http://<host>:8096/health` | 2xx and body `Healthy` ([official endpoint](https://jellyfin.org/docs/general/post-install/networking/advanced/monitoring/)) |
| Navidrome | `http://<host>:4533/ping` | 2xx |
| Ollama | `http://<host>:11434/api/version` | 2xx plus nonempty version string ([official API](https://github.com/ollama/ollama/blob/main/docs/api.md#version)) |
| NextCloud | `http://<host>:8080/status.php` | 2xx plus installed=true, maintenance=false, needsDbUpgrade=false |
| Immich | `http://<host>:2283/api/server/ping` | 2xx plus res=pong |
| Others | any page | 2xx |

Without a URL, and with `DOCKER_STATUS_ENABLED=true` (default), the legacy
services (Jellyfin, Navidrome, MeTube, Bazarr, Ollama, Cloudflare, MCP) report
up when a running container name contains the service name. NextCloud, Immich
and Technical Blog have no assumed container identity, so without a URL they
are `null`; with Docker status disabled, every service without a URL is `null`.

Configured checks issue HTTP(S) GET without redirects or proxy environment
variables. HTTPS verifies certificates. Custom authentication headers and URLs
with embedded credentials are unsupported. A failed configured check returns
false even if its container is running. JSON health responses are limited to
4096 bytes; application-page bodies are not downloaded. Malformed JSON,
missing fields, and unexpected payloads return false.

Checks run concurrently with 0.5-second connection/read socket timeouts.
This is a per-operation timeout, not a strict total deadline: DNS resolution
and slowly delivered headers can take longer; prefer IP addresses in health
URLs. Existing metrics, Docker, and weather timeouts are unchanged.
Cloudflare's container fallback proves only that its process is running,
not that the tunnel is connected.

## Backend startup correction for review

The installed `/etc/systemd/system/homelab-dashboard.service` points to a
missing root-level `server.py`. The prepared
`backend/homelab-dashboard.service.d/override.conf` corrects `ExecStart` to
`backend/server.py`. The user installed it and restarted the API on 2026-10-02; the
active service now executes `backend/server.py`. Photos are served from
`DASHBOARD_PHOTOS_DIR` (default `<project root>/photos-ready`, the same
directory the former `backend/photos-ready` link pointed to).
Review the existing runtime configuration before applying this override. Backend rollout is separate from
firmware upload; `deploy-esp32.sh` is unchanged.

Run isolated tests without contacting production:

```sh
python3 -m py_compile backend/*.py
python3 -m unittest discover -s backend -p 'test_*.py'
```

Deployment verification on 2026-10-02: the restarted API returned HTTP 200,
all seven displayed services true, Windows metrics available, and 20 photos.
An individual photo returned HTTP 200 with image/jpeg content type. The existing
`./deploy-esp32.sh` workflow built both firmware copies and completed OTA to
the display with device result OK. The deployment script and Windows secrets
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
/tmp/test-system-animation
/tmp/test-weather
/tmp/test-navigation
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_ota_animation.cpp -o /tmp/test-ota-animation
/tmp/test-system-animation
/tmp/test-weather
/tmp/test-navigation
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_system_animation.cpp -o /tmp/test-system-animation
/tmp/test-weather
/tmp/test-navigation
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_weather_logic.cpp -o /tmp/test-weather
/tmp/test-navigation
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include firmware/test/test_navigation.cpp -o /tmp/test-navigation
# Needs ArduinoJson from a previous `pio run` (header-only):
g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/include -Ifirmware/.pio/libdeps/esp32dev/ArduinoJson/src firmware/test/test_ai_logic.cpp -o /tmp/test-ai && /tmp/test-ai
/tmp/test-settings
/tmp/test-alerts
/tmp/test-stopwatch
/tmp/test-games
/tmp/test-ota-animation
/tmp/test-system-animation
/tmp/test-weather
/tmp/test-navigation
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
| `AppState`, `Pages.h` | Main-task copy of status data (`app`); pure page enum, root tabs and BACK targets |
| `PageRouter` | `drawCurrentPage()` / `showPage()`; the only file that knows every screen |
| `UiHelpers`, `Display` | Shared header, icon bottom navigation and back bars; the `tft` instance |
| `UiIcons`, `UiIconShapes.h`, `UiTheme.h` | Vector icons from TFT primitives (24x24 shape tables, host-tested) and the accent palette |
| `HomeScreen`, `HomeServerStatusScreen`, `ServicesScreen`, `MenuScreens`, `TimeWeatherScreen`, `CalendarScreen`, `PhotoScreen`, `Screensaver` | One screen each: drawing and its touch zones |
| `HomeLogic.h`, `ServicesLogic.h` | Pure, host-tested HOME health/summary formatting and SERVICES rows |
| `games/TicTacToe`, `games/ReactionGame`, `games/SnakeGame`, `games/MemoryGame`, `games/SimonGame` | Game state, drawing, touch, timing |
| `games/SnakeLogic.h`, `games/MemoryLogic.h`, `games/SimonLogic.h`, `games/GameRandom.h` | Pure, host-tested game rules and millis()-driven state machines |
| `StopwatchScreen`, `Stopwatch.h` | SETTINGS > UTILITIES > STOPWATCH page and its pure, host-tested timing |
| `MenuLayout.h` | Pure, data-driven MORE launcher and GAMES menu layout and hit-testing |
| `TouchHandler` | XPT2046 read, calibration, debounce, wake, backlight boost, dispatch |
| `StatusClient` | FreeRTOS `/api/status` worker, snapshot hand-off, refresh timing |
| `PhotoClient` | FreeRTOS worker for `/api/photos` and JPEG downloads; hands the JPEG buffer to the main task |
| `SettingsScreen`, `DisplaySettingsScreen`, `WifiSettingsScreen` | SETTINGS tab and its sub-pages |
| `DeviceSettings.h`, `SettingsLogic.h` | Pure, host-tested settings model, NVS encoding, navigation and formatting |
| `SettingsStore` | NVS persistence (debounced, change-only writes) and the saved Wi-Fi network |
| `WifiProvisioning` | Temporary setup hotspot and web form, served from its own task |
| `PowerManager`, `BluetoothControl` | Restart, deep sleep, boot-time Bluetooth controller |
| `AlertLogic.h`, `AlertManager`, `AlertsScreen` | Pure, host-tested alert rules; evaluation on each status result; MORE > ALERTS page and the HOMESERVER indicator |
| `AiAssistantScreen`, `AiClient`, `AiLogic.h`, `AiResponse.h` | MORE > AI ASSISTANT menu and the shared answer page (also ASK AI on ALERTS / HOMESERVER); a worker created on first use; pure, host-tested modes, request body, session, parsing and wrapping |
| `PhotoRequestTracker.h` | Pure, host-tested request generations: stale or cancelled photo results are discarded |
| `NetworkManager`, `OtaManager`, `BacklightPwm` | Wi-Fi connect/reconnect, ArduinoOTA, LEDC driver |
| `OtaAnimation`, `OtaAnimationLogic.h` | Walking-man OTA progress screen; pure, host-tested progress mapping and timing |
| `WalkerLogic.h`, `WalkerDraw` | Shared stick-figure walk cycle (OTA and sleep) |
| `WeatherScreen`, `WeatherAnimation`, `WeatherLogic.h`, `WeatherAnimationLogic.h` | WEATHER page and its animated scene; pure, host-tested WMO mapping, day/night, formatting and particle motion |
| `SystemAnimation`, `SystemAnimationLogic.h` | Boot, wake, Wi-Fi, restart, sleep and loading animations; pure, host-tested timelines |

TFT drawing and JPEG decoding happen only on the Arduino loop task; the
status, photo and AI workers only do network I/O and never draw.
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
status refreshes, never counts toward staleness. HOME turns the counts into
HEALTHY / ATTENTION / CRITICAL / NO DATA; HOMESERVER shows `ALL GOOD`,
`2 WARNINGS` or `1 CRITICAL +2` on its RAM line. Tap either, or MORE > ALERTS,
to see the list (three per page, PREV / NEXT); BACK returns where you came from. Thresholds live in
`firmware/include/AlertLogic.h`.

## Navigation

The bottom bar has three tabs with icons, HOME | MORE | SETTINGS (the active
one highlighted; apps count as MORE, the Settings hierarchy and Stopwatch as
SETTINGS). Tabs appear on the three root pages; every page below them has a
BACK bar.

- **HOME**: date, time, current weather (tap: WEATHER), homelab health from
  Alerts (tap: ALERTS) and CPU / RAM (tap: HOMESERVER). Only areas whose data
  changed are redrawn.
- **MORE**: an app launcher of icon tiles (`MORE_APPS` in `MenuLayout.h`;
  eight per page, `<` / `>` in the header switch pages): HomeServer (the
  detailed metrics page that used to be HOME), Services, Weather, Calendar,
  Photos, Alerts, Games and Clocks (the former TIME / WEATHER page with local
  time and world clocks), then AI Assistant on page 2.
- **SETTINGS**: SYSTEM, CONNECTIVITY, DISPLAY, UTILITIES (Stopwatch). The root
  has no BACK; categories return to it.
- **SERVICES**: one row per health check with a green (online), red
  (offline) or grey (not reported) dot.

## Tools and games

BACK goes game > GAMES > MORE.

- **SETTINGS > UTILITIES > STOPWATCH**: start / pause / resume, reset and up to 10 laps
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

## Weather

WEATHER opens from MORE, from HOME's weather summary, or from the Clocks
page's weather block (`FORECAST >`); BACK returns to where it was opened. It
shows the current
temperature, condition and feels-like next to an animated scene; high, low,
rain chance (this hour and today's maximum), humidity, wind and the next
sunrise or sunset; and the next six hours (time, temperature, rain %). Units
are Celsius and km/h like the rest of the dashboard.

The backend extends its single cached Open-Meteo request (15 min) with
`feels_like_c`, `humidity`, `wind_kmh`, `precip_mm`, `precip_probability`,
`precip_probability_max` and `hourly` (up to 8 `{"h","t","p","c"}` entries
from the current hour, selected per request from the cached 48-hour forecast).
Every earlier key is unchanged; any new field may be missing.

`firmware/include/WeatherLogic.h` is the one WMO-code mapping (clear, partly
cloudy, cloudy, fog, drizzle, rain, heavy rain, thunderstorm, snow, unknown).
Day or night comes from today's `sunrise_timestamp` / `sunset_timestamp`. The
scene (sun with turning rays, moon with twinkling stars, drifting clouds,
rain / drizzle / heavy rain, storm with lightning every 3-8 s, snow, fog)
runs at 8 fps only while WEATHER is visible, inside its own 132x84 region;
status updates redraw only text that changed, and the scene restarts only
when the condition or day/night changes. Open-Meteo provides no severe-weather
alerts, so there are none.

## AI Assistant

MORE (page 2) > AI ASSISTANT explains the current homelab status in a few
lines, using the HomeServer's **local Ollama**. Nothing leaves the network
(ESP32 -> HomeServer -> Ollama on the same machine); there is no cloud AI and
no API key. It is **read-only**: it explains and suggests a check, and never
runs commands, restarts anything or changes settings. There is no endpoint
that executes anything. AI text can be wrong; you decide what to do.

The menu asks one of five fixed questions: EXPLAIN STATUS, EXPLAIN ALERTS,
NEEDS ATTENTION, SUGGEST ACTION, SERVER SUMMARY. ASK AI on ALERTS (title bar)
asks about the alerts; ASK AI on HOMESERVER (bottom bar, right) asks what needs
attention. The answer page shows a title, a short summary and a suggested
check, with `AI - LOCAL` (or `RULES - LOCAL`, see below) and the question in
the footer; AGAIN asks again bypassing the cache, BACK returns to where the
question came from (AI menu, ALERTS or HOMESERVER). The answer is kept under
the screensaver; leaving the page discards a pending one. There is no free
text input.

```
ESP32 --POST /api/ai/explain {"mode": "...", "alerts": [codes]}--> backend
backend: get_status() + the dashboard's alert codes -> compact facts -> Ollama
         -> checks -> {"title", "summary", "action"}
```

**Endpoints.** `POST /api/ai/explain` takes `mode` (`status`, `alerts`,
`attention`, `action` or `summary`), optional `refresh` (bool) and optional
`alerts`; any other field (such as `prompt`) is rejected with 400, so the
endpoint is not an Ollama proxy. `alerts` are the dashboard's active alerts
as fixed codes (`{"id": "metube"|"ram"|"disk"|"stale"|..., "level":
"warning"|"critical", "subject": "C:"}`): the alert rules (thresholds,
hysteresis, stale data) live in the firmware, so the AI uses the dashboard's
verdict instead of re-deriving it and can never disagree with HOME. No
metrics come from the ESP32; the backend uses its own `/api/status` data.

```json
{"available": true, "mode": "alerts", "title": "2 items need attention",
 "summary": "MeTube is offline and RAM usage is 87%.",
 "action": "Check the MeTube container status and logs.",
 "source": "ai", "engine": "primary", "generated_at": 1791565000, "cached": false}
```

On failure: `{"available": false, "mode": "...", "error": "AI response timed
out" | "AI service is not responding" | "AI model is not installed" | "AI
returned an unusable answer"}`. `GET /api/ai/status` returns
`{"available", "provider": "ollama", "model"}` (cached 15 s, never generates;
it loads the model in the background so the first answer is quick).

**Engines (primary + fallback, both local).** The backend routes each
question; the ESP32 never talks to Ollama and only learns which engine
answered (`"engine": "primary"` or `"fallback"`, shown as `AI - GPU` /
`AI - CPU`).

| | Primary | Fallback |
|---|---|---|
| Where | GPU laptop on the private link (same endpoint as the blog chatbot) | HomeServer's own Ollama |
| Config | `AI_PRIMARY_OLLAMA_URL` (unset: no primary), `AI_PRIMARY_OLLAMA_MODEL` | `AI_FALLBACK_OLLAMA_URL` (default `http://127.0.0.1:11434`), `AI_FALLBACK_OLLAMA_MODEL` |
| Model | `qwen2.5:7b` (RTX 3070, 4.3 GB VRAM) | `llama3.2:3b` (CPU) |
| Measured | 0.2-0.6 s warm, ~3-15 s cold load | 1.9-4.7 s warm, ~23-25 s cold load |
| Timeout | 8 s (model loaded) / 20 s (cold) | 15 s / 30 s |

The primary address is never in the source: set it in `.env` or the service
environment, for example in a systemd drop-in:
`Environment=AI_PRIMARY_OLLAMA_URL=http://<gpu-host>:11434`.
(`AI_OLLAMA_URL` / `AI_OLLAMA_MODEL` still configure the fallback.)

Routing: a cheap probe (`/api/tags` for the model, `/api/ps` for its load
state; 0.8 s timeout, cached 10 s, never a generation) decides whether the
primary is usable. If it is unreachable or lacks its model, the fallback
answers directly. If a primary generation fails (timeout, connection, HTTP
error, missing model, malformed reply), the fallback is tried exactly once
within a 50 s total budget, and the primary is skipped for 30 s; after that a
probe decides again, so a returning laptop is used automatically. Only if
both fail does the ESP32 get an error. Wording that fails the answer checks
is not retried on the other engine: the rules replace it. Opening the AI menu
loads (never runs) only the engine that will answer, so the HomeServer does
not hold the 2.3 GB CPU model while the laptop is available. Our requests keep
a model loaded 30 minutes; a model another client pinned (the blog uses
`keep_alive: -1`) stays pinned. Each engine keeps one `num_ctx` (2048 on the
laptop, matching the blog's `qwen2.5:7b`, so the shared model is not reloaded).
Models are never downloaded; `/api/ai/status` reports
`{"available", "provider", "preferred": "primary"|"fallback"|null,
"primary_available", "fallback_available"}` without addresses.

**Prompt and checks.** The backend builds a short deterministic snapshot:
the overall verdict, the problems from the dashboard alerts (with their
values), healthy items by name only, offline services that have no alert, and
unknown (unreported) items, which are not problems. No IPs, URLs, Wi-Fi
names, container names or paths. The system prompt allows only those facts,
no trends, no certain causes, safe read-only checks, no commands, plain text,
two lines. Ollama runs with `stream: false`, temperature 0.2, at most 100
tokens and a 1024-token context. The title is computed from the alert counts.
The answer is cleaned (no markdown, URLs, emojis or non-ASCII; title <= 40,
summary <= 220, action <= 140 characters); action clauses that restart,
stop, delete, upgrade, add or change something, or look like a command, are
dropped. If the summary reports problems on a healthy dashboard, misses every
real problem, or calls a warning critical, a rule-based summary and check are
used instead (`"source": "rules"`).

**Timeouts and cache.** See the engine table; one generation per engine at a
time. The ESP32 waits up to 55 s, and BACK always works meanwhile. The server is
threaded, so `/api/status` stays fast during a generation. Answers are cached
60 s per mode and status fingerprint (alerts, services, host availability,
RAM and disks to 5%); AGAIN bypasses it. Failures are not cached.

**Troubleshooting.** `curl http://<homeserver>:8090/api/ai/status`;
`docker ps | grep ollama`; `curl http://127.0.0.1:11434/api/tags` (and the
primary's `http://<gpu-host>:11434/api/tags` from the HomeServer) list the
installed models. `primary_available: false` with the laptop on: check its
`OLLAMA_HOST` and firewall rule for the HomeServer's link address. "AI model is not installed": install it yourself
(`ollama pull llama3.2:3b`) or set `AI_OLLAMA_MODEL`. An Ollama outage only
affects the AI pages, never the dashboard's LIVE / OFFLINE state.

## System animations

Small TFT-primitive animations (no images, task, heap or `delay()`):

- **Boot** (cold start, RST or restart): a server rack's LEDs light one by
  one, links reach three nodes, HOMELAB READY (1.3 s).
- **Wake** (from Settings > Sleep): the moon sets, the sun rises, GOOD
  MORNING (1.1 s). Chosen from the ESP32 wake cause.
- **Wi-Fi** at startup: arcs grow while connecting (with the SSID, never the
  password), CONNECTED with a pulse and the IP (0.6 s), or WI-FI FAILED when
  the boot loop moves on to the other network (0.8 s).
- **Restart**: a gear turns while the rack LEDs go dark (0.9 s), then reboot.
- **Sleep**: the stick figure walks to bed, lies down, the moon and stars come
  out, GOOD NIGHT (2.1 s), then the existing deep sleep.
- `drawLoadingDots()` is a reusable bouncing 3-dot loader (not used yet).

Boot, wake and Wi-Fi play inside the existing blocking connect in `setup()`
while Wi-Fi connects underneath (polled every 20 ms instead of 500 ms), so
they add at most the intro plus 0.6 s when Wi-Fi is instant. Restart and
sleep own the display through `updatePower()`, which makes `loop()` skip
touch, status redraws and the screensaver until the device reboots or
sleeps. Runtime reconnects still show OFFLINE in the header.

## Settings

The SETTINGS tab is a three-level list with an icon per row (BACK goes one
level up; the root's bottom bar is the navigation):

- **SYSTEM**: Device Info, Firmware / Build, Restart, Sleep
- **CONNECTIVITY**: Wi-Fi, Bluetooth
- **DISPLAY**: Brightness, Screensaver, Photos & Wallpaper
- **UTILITIES**: Stopwatch

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
