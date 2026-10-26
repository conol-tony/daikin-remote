#!/usr/bin/env bash
# Back up the firmware regions that flash.sh overwrites (flash table,
# bootloader and the 7 MB "main" region = 0x12000000..0x12720000) in 1 MB
# chunks with retries, since long single reads over the CH343P time out.
#   ./sf32/backup.sh [/dev/ttyACM0]  ->  sf32/backups/<date>/chunk-<addr>.bin + restore.sh
set -euo pipefail
cd "$(dirname "$0")"
. ./env.sh
PORT="${1:-$SF32_PORT}"
DIR="backups/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$DIR"

read_chunk() { # addr size out
    local addr=$1 size=$2 out=$3 attempt
    for attempt in 1 2 3; do
        echo "reading $out ($size bytes @ $addr), attempt $attempt"
        if timeout 400 sftool -c SF32LB52 -m nor -p "$PORT" --compat true --connect-attempts 5 \
               read_flash "$out@$addr:$size" > "$out.log" 2>&1; then
            return 0
        fi
        tail -2 "$out.log"
        sleep 2
    done
    return 1
}

{
    echo "#!/usr/bin/env bash"
    echo "# Restore the backed-up regions: ./restore.sh [/dev/ttyACM0]"
    echo "set -euo pipefail; cd \"\$(dirname \"\$0\")\"; . ../../env.sh; PORT=\"\${1:-\$SF32_PORT}\""
    echo "sftool -c SF32LB52 -m nor -p \"\$PORT\" write_flash --verify \\"
} > "$DIR/restore.sh"

# flash table + bootloader
read_chunk 0x12000000 0x00020000 "$DIR/chunk-0x12000000.bin"
echo "  chunk-0x12000000.bin@0x12000000 \\" >> "$DIR/restore.sh"
# main image region, 7 x 1 MB
for i in 0 1 2 3 4 5 6; do
    addr=$(printf '0x%08X' $((0x12020000 + i * 0x100000)))
    read_chunk "$addr" 0x00100000 "$DIR/chunk-$addr.bin"
    echo "  chunk-$addr.bin@$addr \\" >> "$DIR/restore.sh"
done
echo "" >> "$DIR/restore.sh"
chmod +x "$DIR/restore.sh"
ls -la "$DIR"
echo "backup complete: $DIR (restore with $DIR/restore.sh)"
