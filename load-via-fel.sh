#!/bin/sh

ninja || exit 1

./sunxi-fel -v spl .build/p-boot.bin
#./sunxi-fel -v spl .build/p-boot-silent.bin
#./sunxi-fel -v spl .build/p-boot-serial.bin
