#!/usr/bin/env bash
# Use a separate configuration; never package a developer's personal build.
set -euo pipefail
cd "$(dirname "$0")/.."
rig_release_version="${1:-0.1.0}"
if ! command -v idf.py >/dev/null 2>&1; then
  source "${IDF_PATH:-$HOME/esp/esp-idf-v6.0.1}/export.sh" >/dev/null 2>&1
fi
idf.py -B build-rig-puppy-release -DIDF_TARGET=esp32s3 \
  -DPROJECT_VER="$rig_release_version" \
  -DSDKCONFIG=build-rig-puppy-release/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.rig-puppy;devices/sdkconfig.rig-puppy-release" build
python3 tools/package_release.py --version "$rig_release_version"
