#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

FIRMWARE_DIR="$ROOT_DIR/firmware"
PIO="$ROOT_DIR/.venv-platformio/bin/pio"

WINDOWS_PROJECT="/mnt/c/Users/Deepika/OneDrive/Documents/PlatformIO/Projects/esp32-cyd-homelab"
WINDOWS_MAIN="$WINDOWS_PROJECT/src/main.cpp"
WINDOWS_PLATFORMIO="$WINDOWS_PROJECT/platformio.ini"

WINDOWS_PROJECT_PS='C:\Users\Deepika\OneDrive\Documents\PlatformIO\Projects\esp32-cyd-homelab'
WINDOWS_PIO_PS='C:\Users\Deepika\.platformio\penv\Scripts\platformio.exe'

echo
echo "========================================"
echo " ESP32 Homelab Dashboard Deployment"
echo "========================================"
echo

# --------------------------------------------------
# Validate required files
# --------------------------------------------------

if [ ! -x "$PIO" ]; then
    echo "ERROR: WSL PlatformIO was not found:"
    echo "  $PIO"
    exit 1
fi

if [ ! -f "$FIRMWARE_DIR/src/main.cpp" ]; then
    echo "ERROR: Firmware main.cpp not found."
    exit 1
fi

if [ ! -f "$FIRMWARE_DIR/platformio.ini" ]; then
    echo "ERROR: Firmware platformio.ini not found."
    exit 1
fi

if [ ! -d "$WINDOWS_PROJECT" ]; then
    echo "ERROR: Windows PlatformIO project not found:"
    echo "  $WINDOWS_PROJECT"
    exit 1
fi

# --------------------------------------------------
# Step 1 - Build authoritative WSL firmware
# --------------------------------------------------

echo "[1/3] Building firmware in WSL..."
echo

cd "$FIRMWARE_DIR"

"$PIO" run -e esp32dev-ota

echo
echo "WSL build successful."
echo

# --------------------------------------------------
# Step 2 - Sync source to Windows deployment project
# --------------------------------------------------

echo "[2/3] Syncing firmware to Windows PlatformIO project..."

cp "$FIRMWARE_DIR/src/main.cpp" "$WINDOWS_MAIN"
cp "$FIRMWARE_DIR/platformio.ini" "$WINDOWS_PLATFORMIO"

echo "Source synchronized."
echo
echo "NOTE: Windows include/secrets.h was NOT touched."
echo

# --------------------------------------------------
# Step 3 - Perform OTA using Windows PlatformIO
# --------------------------------------------------

echo "[3/3] Starting OTA upload through Windows PlatformIO..."
echo

powershell.exe -NoProfile -NonInteractive -Command \
    "Set-Location '$WINDOWS_PROJECT_PS'; & '$WINDOWS_PIO_PS' run -e esp32dev-ota -t upload"

echo
echo "========================================"
echo " OTA deployment completed successfully."
echo "========================================"
