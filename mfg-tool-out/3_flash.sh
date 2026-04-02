#!/usr/bin/env bash
# Step 3: Flash the manufacturing partition to the device
set -e

esptool.py -p /dev/ttyACM0 write_flash 0x10000 /home/robert/projects/matter-air-purifier/mfg-tool-out/out/fff1_8002/613944ba-fe9f-48cb-8cb3-5e63c1b6e453/613944ba-fe9f-48cb-8cb3-5e63c1b6e453-partition.bin
