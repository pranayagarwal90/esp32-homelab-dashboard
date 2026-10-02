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