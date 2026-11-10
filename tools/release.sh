#!/usr/bin/env bash
# Builds release images for every device environment into dist/:
#   <project>-<env>-v<version>-full.bin  whole image for USB flashing at 0x0:
#                                        bootloader, partition table, app; the
#                                        gap over NVS is blank, so settings reset
#   <project>-<env>-v<version>-ota.bin   app only, for POST /api/ota on a board
#                                        that already runs this firmware
# plus SHA256SUMS. Runs the native tests first.
# Usage: tools/release.sh
set -euo pipefail
cd "$(dirname "$0")/.."

PIO="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
PY="$(dirname "$PIO")/python"
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"

NAME="$(sed -n 's/^project(\(.*\))/\1/p' CMakeLists.txt)"
VER="$(sed -n 's/^set(PROJECT_VER "\(.*\)")/\1/p' CMakeLists.txt)"
ENVS="$(sed -n 's/^\[env:\(.*\)\]/\1/p' platformio.ini | grep -v '^native$')"
[[ -n "$NAME" && -n "$VER" && -n "$ENVS" ]] || { echo "cannot read project name/version/envs" >&2; exit 1; }

"$PIO" test -e native
rm -rf dist
mkdir -p dist
for env in $ENVS; do
    "$PIO" run -e "$env"
    b=".pio/build/$env"
    out="dist/$NAME-$env-v$VER"
    cp "$b/firmware.bin" "$out-ota.bin"
    # Offsets: bootloader 0x0 (ESP32-S3), partition table 0x8000, otadata and
    # ota_0 from partitions.csv.
    "$PY" "$ESPTOOL" --chip esp32s3 merge_bin -o "$out-full.bin" \
        --flash_mode keep --flash_freq keep --flash_size keep \
        0x0 "$b/bootloader.bin" 0x8000 "$b/partitions.bin" \
        0xf000 "$b/ota_data_initial.bin" 0x20000 "$b/firmware.bin" >/dev/null
done
(cd dist && sha256sum ./*.bin | sed 's# \./# #' > SHA256SUMS)
ls -l dist
