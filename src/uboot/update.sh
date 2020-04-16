#!/bin/sh

UBOOT_DIR=$1

for f in `find -type f`
do
  if test -f $UBOOT_DIR/$f ; then
    cp -f $UBOOT_DIR/$f $f
  else
    echo Missing $f
  fi
done
