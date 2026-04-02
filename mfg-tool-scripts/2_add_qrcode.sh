#!/usr/bin/env bash
# Step 2: Append QR code and manual code strings to the NVS partition so firmware can display them
set -euo pipefail

cd "$(dirname "$0")"

if [[ -z "${IDF_PATH:-}" ]]; then
    echo "Error: IDF_PATH is not set. Source the ESP-IDF environment first." >&2
    exit 1
fi

DEVICE_DIR=$(ls -d out/fff1_8002/*/internal 2>/dev/null | head -1)
if [[ -z "$DEVICE_DIR" ]]; then
    echo "Error: No device directory found under out/fff1_8002/. Run 1_generate.sh first." >&2
    exit 1
fi

DEVICE_UUID_DIR=$(dirname "$DEVICE_DIR")

ONBOARDING_CSV=$(ls "$DEVICE_UUID_DIR/"*-onb_codes.csv 2>/dev/null | head -1)
if [[ -z "$ONBOARDING_CSV" ]]; then
    echo "Error: No onboarding codes CSV found in $DEVICE_UUID_DIR" >&2
    exit 1
fi

QRCODE=$(tail -1 "$ONBOARDING_CSV" | cut -d',' -f1)
MANUALCODE=$(tail -1 "$ONBOARDING_CSV" | cut -d',' -f2)

if [[ -z "$QRCODE" || -z "$MANUALCODE" ]]; then
    echo "Error: Could not parse QR code or manual code from $ONBOARDING_CSV" >&2
    exit 1
fi

if grep -q "^qrcode," "$DEVICE_DIR/partition.csv" || grep -q "^manualcode," "$DEVICE_DIR/partition.csv"; then
    echo "Error: qrcode/manualcode already present in $DEVICE_DIR/partition.csv. Aborting to avoid duplicates." >&2
    exit 1
fi

echo "qrcode,data,string,$QRCODE" >> "$DEVICE_DIR/partition.csv"
echo "manualcode,data,string,$MANUALCODE" >> "$DEVICE_DIR/partition.csv"

python3 "$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" generate \
    "$DEVICE_DIR/partition.csv" \
    "$DEVICE_UUID_DIR/$(basename "$DEVICE_UUID_DIR")-partition.bin" \
    0xC000
