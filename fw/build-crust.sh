#!/bin/sh

test -f config && . ./config
set -e -x

#arm-linux-musleabihf-gcc -static \
#	-DCONFIG_PLATFORM='"sun8i"' \
#	-Iinclude/{common,lib} -Iplatform/sun8i/include \
#	-o load tools/load.c

rm -rf .build-crust

make -j6 -C crust V=1 OBJ=`pwd`/.build-crust pinephone_defconfig
make -j6 -C crust V=1 OBJ=`pwd`/.build-crust 

cp .build-crust/scp/scp.bin .
