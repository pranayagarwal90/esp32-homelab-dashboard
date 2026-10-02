#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

FIRMWARE_DIR="$ROOT_DIR/firmware"
PIO="$ROOT_DIR/.venv-platformio/bin/pio"

WINDOWS_PROJECT="/mnt/c/Users/Deepika/OneDrive/Documents/PlatformIO/Projects/esp32-cyd-homelab"
WINDOWS_MAIN="$WINDOWS_PROJECT/src/main.cpp"
WINDOWS_PLATFORMIO="$WINDOWS_PROJECT/platformio.ini"
WINDOWS_INCLUDE="$WINDOWS_PROJECT/include"

WINDOWS_PROJECT_PS='C:\Users\Deepika\OneDrive\Documents\PlatformIO\Projects\esp32-cyd-homelab'
WINDOWS_PIO_PS='C:\Users\Deepika\.platformio\penv\Scripts\platformio.exe'

echo
echo "========================================"
echo " ESP32 Homelab Dashboard Deployment"
echo "========================================"
echo

# --------------------------------------------------
# Validate required files/directories
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

if [ ! -d "$FIRMWARE_DIR/include" ]; then
    echo "ERROR: Firmware include directory not found:"
    echo "  $FIRMWARE_DIR/include"
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
# Step 2 - Sync firmware to Windows deployment project
# --------------------------------------------------

echo "[2/3] Syncing firmware to Windows PlatformIO project..."
echo

mkdir -p "$WINDOWS_PROJECT/src"
mkdir -p "$WINDOWS_INCLUDE"

cp "$FIRMWARE_DIR/src/main.cpp" "$WINDOWS_MAIN"
cp "$FIRMWARE_DIR/platformio.ini" "$WINDOWS_PLATFORMIO"

echo "  Copied: src/main.cpp"
echo "  Copied: platformio.ini"

HEADER_COUNT=0

while IFS= read -r -d '' header; do
    filename="$(basename "$header")"

    if [ "$filename" = "secrets.h" ]; then
        continue
    fi

    cp "$header" "$WINDOWS_INCLUDE/$filename"
    echo "  Copied: include/$filename"
    HEADER_COUNT=$((HEADER_COUNT + 1))
done < <(find "$FIRMWARE_DIR/include" -maxdepth 1 -type f -print0)

echo
echo "Header files copied: $HEADER_COUNT"
echo "NOTE: include/secrets.h was intentionally NOT copied."
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
