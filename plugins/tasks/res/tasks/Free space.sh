#!/bin/sh
# Shows how much space is free on the SD card. It only reads.
card="${SDCARD_PATH:-/mnt/SDCARD}"
df -Ph "$card"
free=$(df -Ph "$card" | awk 'NR == 2 { print $4 }')
echo "$free free on the SD card"
