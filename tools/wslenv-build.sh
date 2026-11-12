#!/usr/bin/env bash
# Run one PlatformIO command from an ext4 scratch copy of this tree.
#
# PlatformIO on /mnt/* (9p) spends most of its time on filesystem round-trips;
# the same build on the WSL-native filesystem is several times faster. Sources
# are rsynced every run, build output and the first library download live only
# in the scratch copy.
#
#   tools/wslenv-build.sh                      # pio run -e attak-iot-firmware
#   tools/wslenv-build.sh test -e native       # any other pio subcommand
#
# Override the scratch path with ATTAK_BUILD_DIR.
set -euo pipefail

src="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${ATTAK_BUILD_DIR:-/tmp/attak-iot-build}"

if ! command -v rsync >/dev/null 2>&1 && ! command -v tar >/dev/null 2>&1; then
    echo "error: rsync or tar is required" >&2
    exit 1
fi

mkdir -p "$dest"
if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete \
        --exclude=.git \
        --exclude=.pio/build \
        --exclude=.pio/libdeps \
        "$src"/ "$dest"/
else
    # tar fallback: wipe everything except the scratch PlatformIO state so the
    # copy stays exact, then extract the source tree.
    find "$dest" -mindepth 1 -maxdepth 1 ! -name .pio -exec rm -rf {} +
    tar -C "$src" -cf - --exclude='./.git' --exclude='./.pio' . | tar -C "$dest" -xf -
fi

# Seed the library cache from this checkout once; after that the scratch tree
# owns it, so repeat runs skip the 100+ MB copy over 9p.
if [[ ! -d "$dest/.pio/libdeps" && -d "$src/.pio/libdeps" ]]; then
    mkdir -p "$dest/.pio/libdeps"
    cp -a "$src/.pio/libdeps/." "$dest/.pio/libdeps/"
fi

if [[ $# -eq 0 ]]; then
    set -- run -e attak-iot-firmware
fi

cd "$dest"
exec pio "$@"
