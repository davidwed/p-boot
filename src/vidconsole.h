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

struct vidconsole {
	// in chars
	unsigned w;
	unsigned h;
	unsigned size; // w*h

	// in pixels
	uint32_t fb_start;
	uint32_t fb_pitch;
	unsigned fb_width;
	unsigned fb_height;

	unsigned scale;
	uint8_t* font;
	uint32_t* atlas;

	unsigned char* screen;
	uint32_t* fg_color;
	uint32_t* bg_color;
	unsigned cursor;

	uint32_t default_fg;
	uint32_t default_bg;
};

void vidconsole_init(struct vidconsole* c, uint32_t w, uint32_t h, uint32_t scale,
		     uint32_t fg, uint32_t bg);
void vidconsole_redraw(struct vidconsole* console);
void vidconsole_putc(struct vidconsole* console, char c);
void vidconsole_set_xy(struct vidconsole* console, unsigned x, unsigned y,
		       char ch, uint32_t fg, uint32_t bg);
