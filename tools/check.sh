#!/usr/bin/env bash
# Local guardrail: exactly what .github/workflows/ci.yml runs.
#
#   tools/check.sh
#
# On /mnt/* it goes through tools/wslenv-build.sh, because a direct PlatformIO
# build on 9p is several times slower.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ "$(pwd)" == /mnt/* ]]; then
    tools/wslenv-build.sh test -e native
    tools/wslenv-build.sh run -e attak-iot-firmware
    tools/wslenv-build.sh run -e i2c-scan
else
    pio test -e native
    pio run -e attak-iot-firmware
    pio run -e i2c-scan
fi
