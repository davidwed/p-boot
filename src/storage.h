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

#include <common.h>
#include <asm/arch/mmc.h>
#include <asm/arch/gpio.h>
#include <asm/arch/clock.h>
#include <asm-generic/gpio.h>
#include <stdlib.h>
#include <fdt_support.h>
#include <mmc.h>
#include "bootfs.h"

struct bootfs {
	struct mmc* mmc;
	uint64_t mmc_offset;

	struct bootfs_sb* sb;
	// the following 2 pointers overlap
	struct bootfs_conf* confs_blocks;
	struct bootfs_files* files_blocks;
};

struct mmc* mmc_probe(int mmc_no);
bool mmc_read_data(struct mmc* mmc, uintptr_t dest, uint64_t off, uint32_t len);

struct bootfs* bootfs_open(struct mmc* mmc);
ssize_t bootfs_load_image(struct bootfs* fs, uint32_t dest,
			  uint64_t off, uint32_t len, const char* name);
ssize_t bootfs_load_file(struct bootfs* fs, uint32_t dest, const char* name);
