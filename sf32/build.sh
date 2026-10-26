#!/usr/bin/env bash
# Build the T-Display-SF32 firmware: ./sf32/build.sh [-j8] [--clean]
set -euo pipefail
cd "$(dirname "$0")"
. ./env.sh
cd project
if [ "${1:-}" = "--clean" ]; then
    rm -rf "build_${SF32_BOARD}"
    shift
fi
rm -f Kconfig.tmp
scons --board="$SF32_BOARD" "${@:--j4}"
echo
echo "Images in sf32/project/build_${SF32_BOARD}/ (main.bin, bootloader/, ftab/)"
