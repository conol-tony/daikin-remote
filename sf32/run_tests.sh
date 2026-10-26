#!/usr/bin/env sh
# Exercise the production UI dispatch on the host without the board SDK.
set -eu
cd "$(dirname "$0")"
test_build_dir=$(mktemp -d "${TMPDIR:-/tmp}/daikin-sf32-tests.XXXXXX")
trap 'rm -rf "$test_build_dir"' EXIT HUP INT TERM

for profile in cooling heat_pump; do
    profile_flags=
    if [ "$profile" = heat_pump ]; then profile_flags=-DREMOTE_HEAT_PUMP; fi
    "${CC:-cc}" -std=c99 -O1 -Wall -Wextra -Wpedantic -Werror \
        -ffunction-sections -fdata-sections $profile_flags \
        -Itest/stubs -I../protocol test/test_ui.c ../protocol/daikin_arc466.c \
        -Wl,--gc-sections -o "$test_build_dir/test_ui_$profile"
    "$test_build_dir/test_ui_$profile"
done
