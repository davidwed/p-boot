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

#include <stdint.h>

// all values are BE

struct bootfs_sb {
	uint8_t magic[8]; // :BOOTFS:
	uint32_t version; // 1
	uint32_t default_conf;
	uint8_t device_id[32];
	uint8_t res[2048-8-4-4-32];
};

struct bootfs_image {
	uint32_t type; // 'L' Linux, 'A' ATF 'I' Initramfs 'D' DTB
	uint32_t data_off; // aligned to sector (512B)
	uint32_t data_len; // unaligned, bootloader must align
};

// takes 2048B
struct bootfs_conf {
	uint8_t magic[8]; // :BFCONF:
	struct bootfs_image images[8]; // type=0 == unused entry
	uint8_t boot_args[2048 - 8 - 8 * sizeof(struct bootfs_image) - 96]; // null terminated string
	uint8_t name[96];
};

// takes 40B
struct bootfs_file {
	uint8_t name[32];
	uint32_t data_off; // aligned to sector (512B)
	uint32_t data_len; // unaligned, bootloader must align
};

// (2048 - 8) / 40B
struct bootfs_files {
	uint8_t magic[8]; // :BFILES:
	struct bootfs_file files[51];
};

// layout

// off (KiB) |
// ------------------------------------
// 0         | (bootfs_sb){1}
// 2         | (bootfs_conf|bootfs_files){32}
// 128       | (data...) each data block is aligned to 512B
