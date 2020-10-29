#!/bin/sh

set -e -x

rm -rf pngs argb
mkdir -p pngs argb

for f in *.svg
do
	out=${f%.svg}
	
	inkscape -y 1 -b black --export-type=png -o pngs/$out.png $f
	convert pngs/$out.png argb/$out.bgra
	mv argb/$out.bgra argb/$out.argb
done

for f in *.png
do
	out=${f%.png}
	convert $out.png argb/$out.bgra
	mv argb/$out.bgra argb/$out.argb
done

rm -rf pngs
