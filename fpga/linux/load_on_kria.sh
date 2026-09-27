#!/bin/bash
set -eu
SRC=/tmp/tile_brightness_pkg
APP=/lib/firmware/xilinx/tile_brightness
mkdir -p "$APP"
cp "$SRC/tile_brightness.bit.bin" /lib/firmware/tile_brightness.bit.bin
cp "$SRC/tile_brightness.bit.bin" "$APP/tile_brightness.bit.bin"
cp "$SRC/shell.json" "$APP/shell.json"
dtc -@ -I dts -O dtb -o "$APP/tile_brightness.dtbo" "$SRC/tile_brightness.dts"
cp "$APP/tile_brightness.dtbo" "$SRC/tile_brightness.dtbo"
echo 4 > /proc/sys/vm/nr_hugepages || true
modprobe uio_pdrv_genirq of_id=generic-uio || true
if xmutil listapps >/dev/null 2>&1; then
    xmutil unloadapp || true
    xmutil loadapp tile_brightness
else
    mkdir -p /sys/kernel/config/device-tree/overlays/tile_brightness
    cat "$APP/tile_brightness.dtbo" > /sys/kernel/config/device-tree/overlays/tile_brightness/dtbo
fi
echo "--- uio ---"
ls -l /dev/uio* 2>/dev/null || echo "no uio"
echo "--- hugepages ---"
grep -E 'HugePages' /proc/meminfo
echo "--- fpga state ---"
cat /sys/class/fpga_manager/fpga0/state
