# SF32 SDK preparation

Run `./sf32/setup-sdk.sh` from the project root on Linux x86_64. It requires
Bash, Git, tar, xz, curl, sha256sum, uv, and network access. It prepares
`sdk/sifli-sdk`, installs Python 3.12 through uv, runs the SDK's SF32LB52
installer, and installs checksum-verified sftool 0.2.5 in
`sdk/tools/sftool-0.2.5`. The SDK installer manages its Python environment and
toolchain under `~/.sifli`; its dependency downloads follow the pinned SDK's
own manifests and constraints. This is a pinned source setup, not a fully
offline or byte-for-byte reproducible dependency lock.

The script refuses to overwrite an existing SDK directory. A separate source
tree can be prepared without installing tools:

```sh
./sf32/setup-sdk.sh --sources-only --sdk-dir /tmp/daikin-sifli-sdk
```

The existing build and flash helpers use `sdk/sifli-sdk`. The alternate target
is useful for inspecting and comparing sources. For offline preparation from
existing Git clones containing the pinned commits:

```sh
SF32_LILYGO_SOURCE=/path/to/SlFli-SDK-Lilygo \
SF32_UPSTREAM_SOURCE=/path/to/SiFli-SDK \
    ./sf32/setup-sdk.sh --sources-only --sdk-dir /tmp/daikin-sifli-sdk
```

Source pins:

| Source | Commit | Files used |
| --- | --- | --- |
| [LilyGo SDK](https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo/tree/bd4f9945a7638f2f418d4b3c3e5495c6c6c5238d) | `bd4f9945a7638f2f418d4b3c3e5495c6c6c5238d` | `sifli-sdk/` |
| [Upstream SiFli SDK v2.4.1](https://github.com/OpenSiFli/SiFli-SDK/tree/6fda48ba4da89a0e79509cb9ef971f0d13c97ec7) | `6fda48ba4da89a0e79509cb9ef971f0d13c97ec7` | `tools/build/` |

The LilyGo repository omits `tools/build/` because of its ignore rules; the
script restores this directory from upstream v2.4.1 before applying the
patches. The TCA8418, SGM41562B, and XL9555 RT-Thread device adapters are
already included in the pinned LilyGo sources.

Patches are applied in filename order:

- `0001-posix-build-resource.patch` resolves a name collision between the
  SDK's `resource.py` and the POSIX Python standard-library module. The
  upstream build helper is copyright 2006–2015 RT-Thread Development Team,
  licensed GPL-2.0-or-later. The patch follows that license. The helper runs
  on the host and is not linked into the firmware.
- `0002-charger-failed-init-cleanup.patch` detaches the IRQ semaphore before
  freeing a charger handle after failed initialization. The upstream charger
  driver is copyright 2024 RT-Thread Development Team, licensed Apache-2.0.
  The patch follows that license.

The setup script is MIT-licensed. SDK components and these patches retain
their own licenses; see [third-party notices](../THIRD_PARTY_NOTICES.md).

If tool installation fails after sources have been prepared, resolve the
reported prerequisite or network issue, then resume the SDK's own installer
with a non-virtualenv Python 3.12 on `PATH`:

```sh
uv python install 3.12
export PATH="$(dirname "$(uv python find 3.12 --no-project)"):$PATH"
unset VIRTUAL_ENV
bash sdk/sifli-sdk/install.sh sf32lb52
```

The extra sftool 0.2.5 download is needed by `sf32/backup.sh`; the SDK's bundled
0.1.7 lacks `read_flash`. Its Linux x86_64 archive has SHA-256
`ab12738de1be0277f0304a217a3773bea9495d088b438839c7739150ee37a905`.
