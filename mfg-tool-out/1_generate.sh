#!/usr/bin/env bash
# Step 1: Generate manufacturing partition using esp-matter-mfg-tool
set -e

esp-matter-mfg-tool --vendor-id 0xFFF1 --product-id 0x8002 --vendor-name Twinsen --product-name "Air Purifier" --hw-ver 1 --hw-ver-str "01"
