#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Fetch the exact SDK sources used by this firmware and install Linux tools.
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_dir="$project_root/sdk/sifli-sdk"
sources_only=false
lilygo_commit=bd4f9945a7638f2f418d4b3c3e5495c6c6c5238d
upstream_commit=6fda48ba4da89a0e79509cb9ef971f0d13c97ec7
lilygo_source="${SF32_LILYGO_SOURCE:-https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo.git}"
upstream_source="${SF32_UPSTREAM_SOURCE:-https://github.com/OpenSiFli/SiFli-SDK.git}"

usage() {
    cat <<'EOF'
Usage: ./sf32/setup-sdk.sh [--sources-only] [--sdk-dir DIR]

Prepares a new SDK directory; existing directories are never overwritten.
By default, installs Python 3.12 with uv, the SDK's SF32LB52 toolchain,
and sftool 0.2.5 for Linux x86_64. Downloads require network access.
--sources-only skips Python/tool installation and works on other platforms.
--sdk-dir changes the output directory (default: sdk/sifli-sdk).

For cached/offline source preparation, SF32_LILYGO_SOURCE and
SF32_UPSTREAM_SOURCE may name local Git repositories containing the pins.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --sources-only) sources_only=true; shift ;;
        --sdk-dir)
            if [ "$#" -lt 2 ] || [ -z "$2" ]; then
                echo "--sdk-dir requires a directory" >&2
                exit 1
            fi
            sdk_dir="$2"
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 1 ;;
    esac
done

if [ -e "$sdk_dir" ] || [ -L "$sdk_dir" ]; then
    echo "Refusing to overwrite existing SDK: $sdk_dir" >&2
    echo "Use --sdk-dir with a new directory to prepare a separate copy." >&2
    exit 1
fi
for dependency in git tar; do
    command -v "$dependency" >/dev/null || {
        echo "Missing required command: $dependency" >&2
        exit 1
    }
done
if ! "$sources_only"; then
    if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
        echo "Tool installation supports Linux x86_64; use --sources-only elsewhere." >&2
        exit 1
    fi
    for dependency in uv curl xz sha256sum; do
        command -v "$dependency" >/dev/null || {
            echo "Missing required command: $dependency" >&2
            exit 1
        }
    done
fi

mkdir -p "$(dirname "$sdk_dir")"
sdk_parent="$(cd "$(dirname "$sdk_dir")" && pwd)"
sdk_dir="$sdk_parent/$(basename "$sdk_dir")"
staging="$(mktemp -d "$sdk_parent/.sf32-setup.XXXXXX")"
trap 'rm -rf "$staging"' EXIT

export_tree() {
    local source_repo="$1" commit="$2" subtree="$3" destination="$4" cache_name="$5"
    if [ ! -d "$source_repo" ]; then
        local remote_url="$source_repo"
        source_repo="$staging/$cache_name"
        git init --quiet "$source_repo"
        git -C "$source_repo" remote add origin "$remote_url"
        git -C "$source_repo" config remote.origin.promisor true
        git -C "$source_repo" config remote.origin.partialclonefilter blob:none
        git -C "$source_repo" fetch --quiet --depth=1 --filter=blob:none origin "$commit"
    fi
    git -C "$source_repo" cat-file -e "$commit^{commit}"
    mkdir -p "$destination"
    git -C "$source_repo" archive "$commit:$subtree" | tar -x -C "$destination"
}

echo "Preparing pinned LilyGo SDK sources..."
export_tree "$lilygo_source" "$lilygo_commit" sifli-sdk "$staging/sifli-sdk" lilygo
echo "Restoring upstream v2.4.1 build helpers..."
export_tree "$upstream_source" "$upstream_commit" tools/build "$staging/sifli-sdk/tools/build" upstream

for patch_file in "$project_root"/sdk-patches/*.patch; do
    echo "Applying $(basename "$patch_file")"
    # Keep git apply independent of this project's surrounding Git checkout.
    GIT_CEILING_DIRECTORIES="$staging" git -C "$staging/sifli-sdk" apply --check "$patch_file"
    GIT_CEILING_DIRECTORIES="$staging" git -C "$staging/sifli-sdk" apply "$patch_file"
done
cat > "$staging/sifli-sdk/.daikin-remote-sources" <<EOF
LilyGo SDK: https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo/tree/$lilygo_commit/sifli-sdk
Build helpers: https://github.com/OpenSiFli/SiFli-SDK/tree/$upstream_commit/tools/build
Local changes: daikin-remote/sdk-patches/*.patch
EOF
mv "$staging/sifli-sdk" "$sdk_dir"
echo "SDK sources ready: $sdk_dir"

if "$sources_only"; then
    exit 0
fi

uv python install 3.12
sf32_python="$(uv python find 3.12 --no-project)"
export PATH="$(dirname "$sf32_python"):$PATH"
unset VIRTUAL_ENV
bash "$sdk_dir/install.sh" sf32lb52

# The SDK pins sftool 0.1.7; backups require the read_flash support in 0.2.5.
sftool_dir="$sdk_parent/tools/sftool-0.2.5"
if [ -e "$sftool_dir" ] || [ -L "$sftool_dir" ]; then
    echo "Keeping existing sftool directory: $sftool_dir"
else
    archive="$staging/sftool.tar.xz"
    curl --fail --location --retry 3 \
        https://github.com/OpenSiFli/sftool/releases/download/0.2.5/sftool-0.2.5-x86_64-unknown-linux-gnu.tar.xz \
        --output "$archive"
    printf '%s  %s\n' ab12738de1be0277f0304a217a3773bea9495d088b438839c7739150ee37a905 "$archive" | sha256sum --check
    mkdir "$staging/sftool"
    tar -xJf "$archive" -C "$staging/sftool"
    mkdir -p "$(dirname "$sftool_dir")"
    mv "$staging/sftool" "$sftool_dir"
fi
echo "Setup complete. With the default SDK directory, run ./sf32/build.sh"
