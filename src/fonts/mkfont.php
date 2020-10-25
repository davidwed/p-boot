#!/usr/bin/env php
<?php

$bin = file_get_contents($argv[1]);
$lw = strlen($bin) / 256;

echo "$lw\n\n";

for ($co = 0; $co < strlen($bin); $co += $lw) {
	printf("0x%02x\n", $co / $lw);

	for ($y = 0; $y < $lw; $y++) {
		$l = ord($bin[$co + $y]);
		for ($x = 0; $x < 8; $x++)
			echo ($l & (1 << (7 - $x))) ? '#' : '.';
		echo "\n";
	}
	echo "\n\n";
}