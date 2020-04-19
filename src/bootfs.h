/**
 * p-boot - pico sized bootloader
 *
 * Copyright (C) 2020  Ondřej Jirman <megi@xff.cz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// all values are BE

// takes 512B
struct bootfs_sb {
	char magic[8]; // :BOOTFS:
	unsigned int version; // 1
	unsigned int default_conf;
};

struct bootfs_image {
	unsigned int type; // 'L' Linux, 'A' ATF 'I' Initramfs 'D' DTB
	unsigned int data_off; // aligned to sector (512B)
	unsigned int data_len; // unaligned, bootloader must align
};

// takes 2048B
struct bootfs_conf {
	char magic[8]; // :BFCONF:
	struct bootfs_image images[8]; // type=0 == unused entry
	char boot_args[2048 - 8 - 8*4*3]; // null terminated string
};

// layout

// bootfs_sb starts
// [bootfs_conf...]
// [data...] each image data block is aligned to 512B, first starts at 128kB to leave space for boot entries