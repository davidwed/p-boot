#!/bin/sh

ninja || exit 1

./sunxi-fel -v spl .build/p-boot.bin
