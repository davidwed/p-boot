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

#include <stdbool.h>

void display_board_init(void);
bool display_init(void);
void backlight_enable(uint32_t pct);

struct display {
	struct display_plane {
		uint32_t fb_start; // 0 = disabled
		uint32_t fb_pitch;

		uint32_t src_x;
		uint32_t src_y;
		uint32_t src_w;
		uint32_t src_h;

		int32_t dst_x;
		int32_t dst_y;
		uint32_t dst_w;
		uint32_t dst_h;

		uint32_t alpha;

		uint32_t z; // 0 = bottom, 2 = top
	} planes[3];
};

void display_commit(struct display* d);
bool display_frame_done(void);

#define PANEL_WIDTH		(720)
#define PANEL_HEIGHT		(1440)
