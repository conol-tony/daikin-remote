# Source this file to get the SiFli SDK build environment:
#   . sf32/env.sh
# It needs a non-virtualenv Python 3.9+ on PATH (the SDK's legacy installer
# refuses to run from inside a venv); a uv-managed interpreter is used here.
_sf32_dir="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
_py="$(uv python find 3.12 --no-project 2>/dev/null)"
if [ -n "$_py" ]; then
    export PATH="$(dirname "$_py"):$PATH"
fi
unset VIRTUAL_ENV
# shellcheck disable=SC1091
. "$_sf32_dir/../sdk/sifli-sdk/export.sh" > /dev/null
# The SDK's bundled sftool 0.1.7 lacks read_flash; prefer the 0.2.5 release.
if [ -x "$_sf32_dir/../sdk/tools/sftool-0.2.5/sftool" ]; then
    export PATH="$(cd "$_sf32_dir/../sdk/tools/sftool-0.2.5" && pwd):$PATH"
fi
export SF32_BOARD=t-display-sf32_hcpu
export SF32_PORT="${SF32_PORT:-/dev/ttyACM0}"
echo "SiFli SDK ready: SIFLI_SDK=$SIFLI_SDK board=$SF32_BOARD port=$SF32_PORT"
unset _sf32_dir _py
