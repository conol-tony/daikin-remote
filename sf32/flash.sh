#!/usr/bin/env bash
# Flash the built firmware over the USB-C serial port (CH343P, 1 Mbps).
#   ./sf32/flash.sh [/dev/ttyACM0]
# sftool talks to the SF32LB52's UART debug port, so no BOOT button is needed.
# The link is somewhat flaky on this board (compat mode is required and a
# transfer occasionally times out), so the write is retried a few times. If
# every attempt fails, re-plug the USB cable (LilyGo's advice) and run again;
# the battery must be connected or the charger IC keeps resetting the board.
set -uo pipefail
cd "$(dirname "$0")"
. ./env.sh
PORT="${1:-$SF32_PORT}"
BUILD="project/build_${SF32_BOARD}"
[ -f "$BUILD/main.bin" ] || { echo "no build found, run ./sf32/build.sh first"; exit 1; }
cd "$BUILD"
for attempt in 1 2 3 4; do
    echo "=== flash attempt $attempt ==="
    if sftool -c SF32LB52 -m nor -p "$PORT" --compat true --connect-attempts 5 write_flash --verify \
        bootloader/bootloader.bin@0x12010000 main.bin@0x12020000 ftab/ftab.bin@0x12000000; then
        echo "flash OK"
        exit 0
    fi
    sleep 3
done
echo "flash FAILED after 4 attempts; re-plug the USB cable and retry" >&2
exit 1
