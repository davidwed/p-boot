#!/bin/sh

set -e -x

./build-atf.sh
./build-crust.sh

#
# ATF is loaded at 0x44000      fw.bin offset 0
# Crust is loaded at 0x50000    fw.bin offset 48 * 2^10
#

cp bl31.bin fw.bin
dd if=scp.bin of=fw.bin bs=1k seek=48

cp bl31-debug.bin fw-debug.bin
dd if=scp.bin of=fw-debug.bin bs=1k seek=48

#scp scp.bin root@pp-usb:~/
