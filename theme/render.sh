#!/bin/sh

set -e -x

rm -rf pngs argb
mkdir -p pngs argb

gcc -o make-argb argb.c

for f in *.svg
do
	out=${f%.svg}
	
	inkscape -y 1 -b black --export-type=png -o pngs/$out.png $f
	convert pngs/$out.png argb/$out.rgba
	./make-argb argb/$out.rgba argb/$out.argb
	rm argb/$out.rgba
done

