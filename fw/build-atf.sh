#!/bin/sh

test -f config && . ./config
set -e -x

export CROSS_COMPILE=aarch64-linux-musl-

rm -rf "atf/build"

make -j6 -C atf PLAT=sun50i_a64 DEBUG=0 bl31
cp "atf/build/sun50i_a64/release/bl31.bin" bl31.bin

make -j6 -C atf PLAT=sun50i_a64 DEBUG=1 bl31
cp "atf/build/sun50i_a64/debug/bl31.bin" bl31-debug.bin
