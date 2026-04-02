#!/usr/bin/env bash
# Step 2: Append QR code and manual code strings to the NVS partition so firmware can display them
set -e

DEVICE_DIR=$(ls -d out/fff1_8002/*/internal | head -1)
DEVICE_UUID_DIR=$(dirname "$DEVICE_DIR")

QRCODE=$(tail -1 "$DEVICE_UUID_DIR/"*-onb_codes.csv | cut -d',' -f1)
MANUALCODE=$(tail -1 "$DEVICE_UUID_DIR/"*-onb_codes.csv | cut -d',' -f2)

echo "qrcode,data,string,$QRCODE" >> "$DEVICE_DIR/partition.csv"
echo "manualcode,data,string,$MANUALCODE" >> "$DEVICE_DIR/partition.csv"

python3 $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate \
    "$DEVICE_DIR/partition.csv" \
    "$DEVICE_UUID_DIR/$(basename "$DEVICE_UUID_DIR")-partition.bin" \
    0xC000
