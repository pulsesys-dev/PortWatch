#!/usr/bin/env bash
# Builds PortWatch for both boards: the full flash images for the browser flasher, the app-only
# files for updates over Wi-Fi, the flasher page with both firmwares inside, and the package zip.
# Used by .github/workflows/build.yml. Also runs locally once arduino-cli, the ESP32 core 3.1.1
# and the libraries are installed (same commands as the workflow).
#
#   firmware/tools/build_release.sh      -> build/release/  and  build/site/ (for GitHub Pages)
#
# Environment (optional):
#   ARDUINO_CLI  arduino-cli command   (default: arduino-cli)
#   ESPTOOL      esptool command       (default: python3 -m esptool)
set -euo pipefail

FW=$(cd "$(dirname "$0")/.." && pwd)          # firmware/
ROOT=$(dirname "$FW")                          # repo root
CLI=${ARDUINO_CLI:-arduino-cli}
ESPTOOL=${ESPTOOL:-python3 -m esptool}
BUILD="$ROOT/build"
REL="$BUILD/release"
OUT="$FW/out"
VERSION=$(sed -n 's/^#define EP_VERSION *"\([^"]*\)".*/\1/p' "$FW/PortWatch/config.h")
echo "PortWatch $VERSION"
rm -rf "$REL" "$BUILD/site"; mkdir -p "$REL" "$OUT" "$BUILD/site/web"

BOOT_APP0=$(find "$($CLI config get directories.data 2>/dev/null || echo "$HOME/.arduino15")" \
                 -path '*esp32*' -name boot_app0.bin 2>/dev/null | head -1)
[ -n "$BOOT_APP0" ] || { echo "boot_app0.bin not found - is the ESP32 core installed?"; exit 1; }

build() {   # name  fqbn  chip  bootloader-offset
  local name=$1 fqbn=$2 chip=$3 boot=$4 dir="$BUILD/$1"
  echo "== $name ($fqbn)"
  "$CLI" compile --fqbn "$fqbn" --build-path "$dir" --warnings default "$FW/PortWatch"
  $ESPTOOL --chip "$chip" merge_bin -o "$OUT/PortWatch-$name.bin" \
      --flash_mode keep --flash_freq keep --flash_size 4MB \
      "$boot" "$dir/PortWatch.ino.bootloader.bin" 0x8000 "$dir/PortWatch.ino.partitions.bin" \
      0xe000 "$BOOT_APP0" 0x10000 "$dir/PortWatch.ino.bin"
  cp "$dir/PortWatch.ino.bin" "$OUT/PortWatch-$name.ota.bin"
  cp "$OUT/PortWatch-$name.bin" "$OUT/PortWatch-$name.ota.bin" "$REL/"
}

# CYD: USB is a CH340 serial chip on UART0
build cyd     "esp32:esp32:esp32:FlashSize=4M,PartitionScheme=min_spiffs" esp32 0x1000
# Guition JC4827W543: 8 MB OPI PSRAM, USB goes straight to the ESP32-S3 (USB CDC on boot)
build guition "esp32:esp32:esp32s3:FlashSize=4M,PSRAM=opi,FlashMode=qio,PartitionScheme=min_spiffs,CDCOnBoot=cdc,USBMode=hwcdc" esp32s3 0x0

python3 "$FW/tools/build_flasher.py" "$OUT"
cp "$OUT/PortWatch-Flasher.html" "$REL/"

# GitHub Pages site: the flasher (firmware inside) at the top, the web dashboard under /web/
cp "$OUT/PortWatch-Flasher.html" "$BUILD/site/index.html"
cp "$ROOT/PortWatch-Web.html" "$BUILD/site/web/index.html"
touch "$BUILD/site/.nojekyll"

# the package, as people unzip it (with the firmware-filled flasher)
PKG="$BUILD/pkg/PortWatch"
rm -rf "$BUILD/pkg"; mkdir -p "$PKG"
cp "$ROOT/README.md" "$ROOT/PortWatch-Web.html" "$PKG/"
cp "$OUT/PortWatch-Flasher.html" "$PKG/"
cp -R "$FW" "$PKG/firmware" && rm -rf "$PKG/firmware/out" "$PKG/firmware/.pio"
mkdir -p "$PKG/firmware/out" && cp "$OUT"/PortWatch-*.bin "$PKG/firmware/out/"
(cd "$BUILD/pkg" && zip -qr "$REL/PortWatch-$VERSION.zip" PortWatch -x "*.DS_Store" "*__pycache__*")

echo "Release files in build/release:"
ls -l "$REL"
