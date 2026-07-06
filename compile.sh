#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
FQBN="esp32:esp32:waveshare_esp32_s3_touch_amoled_18:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=enabled,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600"
arduino-cli compile --fqbn "$FQBN" \
  --build-property "build.partitions=app3M_fat9M_16MB" \
  --build-property "upload.maximum_size=3145728" \
  --library lib/Matrix --library lib/GFX_Lite \
  --build-path "$SCRIPT_DIR/build/esp32.esp32.esp32s3" .
echo "Build complete."
