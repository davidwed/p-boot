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

#include <common.h>
#include <malloc.h>
#include <arm_neon.h>
#include <asm/armv8/mmu.h>
#include <asm/system.h>
#include <cpu_func.h>
#include "font.h"
#include "vidconsole.h"

#if 0
#define FONT_NAME font_8x8_half
#define FONT_WIDTH 8
#define FONT_HEIGHT 8
#define FONT_CHARS 128
#elif 0
#define FONT_NAME font_8x16
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define FONT_CHARS 256
#elif 1
#define FONT_NAME font_8x16_half
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define FONT_CHARS 128
#endif

static void flush_cache_auto_align(void* buf, size_t len)
{
	uintptr_t mask = ~((uintptr_t)CONFIG_SYS_CACHELINE_SIZE - 1);
	uintptr_t start = (uintptr_t)buf & mask;

	len = (len + 2 * CONFIG_SYS_CACHELINE_SIZE) & mask;

	flush_cache(start, len);
}

void vidconsole_init(struct vidconsole* c, uint32_t w, uint32_t h, uint32_t scale,
		     uint32_t fg, uint32_t bg)
{
	memset(c, 0, sizeof *c);

	c->w = w;
	c->h = h;
	c->size = w * h;

	c->scale = scale;

	c->fb_width = w * FONT_WIDTH * scale;
	c->fb_height = h * FONT_HEIGHT * scale;
	c->fb_pitch = c->fb_width * 4;
	c->fb_start = (uintptr_t)malloc(c->fb_pitch * c->fb_height);

	c->screen = malloc(c->size);
	for (int i = 0; i < c->size; i++)
		c->screen[i] = ' ';

	c->default_fg = fg;
	c->default_bg = bg;
	c->fg_color = malloc(c->size * 4);
	c->bg_color = malloc(c->size * 4);
	for (unsigned i = 0; i < c->size; i++) {
		c->fg_color[i] = fg;
		c->bg_color[i] = bg;
	}

        // build glyph atlas

	c->atlas = malloc(FONT_WIDTH * FONT_HEIGHT * FONT_CHARS * scale);

	// in pixels
	unsigned char_stride = FONT_WIDTH * scale;
	unsigned char_size = char_stride * FONT_HEIGHT;

	for (unsigned ch = 0; ch < FONT_CHARS; ch++) {
		for (unsigned y = 0; y < FONT_HEIGHT; y++) {
			uint8_t ln_data = FONT_NAME[ch * FONT_HEIGHT + y];

			for (unsigned x = 0; x < FONT_WIDTH; x++) {
				uint32_t pix = ln_data & 0x80 ? 0xffffffff : 0;

				for (unsigned s = 0; s < c->scale; s++)
					c->atlas[ch * char_size + y * char_stride + c->scale * x + s] = pix;

				ln_data <<= 1;
			}
		}
	}
}

void vidconsole_set_xy(struct vidconsole* c, unsigned x, unsigned y, char ch, uint32_t fg, uint32_t bg)
{
	unsigned pos = x + y * c->w;

	c->screen[pos] = ch;
	c->fg_color[pos] = fg;
	c->bg_color[pos] = bg;
}

void vidconsole_redraw(struct vidconsole* c)
{
	uint32_t* fb = (uint32_t*)(uintptr_t)c->fb_start;
	unsigned char_stride = FONT_WIDTH * c->scale;
	unsigned char_size = char_stride * FONT_HEIGHT;

	for (unsigned x = 0; x < c->w; x++) {
		for (unsigned y = 0; y < c->h; y++) {
			unsigned pos = y * c->w + x;
			unsigned ch = c->screen[pos];
			if (ch > FONT_CHARS)
				ch = '?';
			uint32x4_t bg_v = vdupq_n_u32(c->bg_color[pos]);
			uint32x4_t fg_v = vdupq_n_u32(c->fg_color[pos]);

			uint32_t *con_p = fb + c->fb_width * y * FONT_HEIGHT * c->scale + x * char_stride;

			for (unsigned cy = 0; cy < FONT_HEIGHT; cy++) {
				for (unsigned s = 0; s < c->scale; s++) {
					uint32_t *dst = con_p + c->fb_width * (c->scale * cy + s);
					uint32_t *src = c->atlas + char_size * ch + char_stride * cy;
					uint32_t len = char_stride;

					while (len >= 4 * 2) {
						uint32x4_t v0 = vld1q_u32(src);
						v0 = vbslq_u32(v0, fg_v, bg_v);
						uint32x4_t v1 = vld1q_u32(src + 4);
						v1 = vbslq_u32(v1, fg_v, bg_v);
						vst1q_u32(dst, v0);
						vst1q_u32(dst + 4, v1);

						src += 4 * 2;
						dst += 4 * 2;
						len -= 4 * 2;
					}
				}
			}
		}
	}

	flush_cache_auto_align(fb, c->fb_height * c->fb_pitch);
}

void vidconsole_putc(struct vidconsole* c, char ch)
{
	if (!c)
		return;

	if (ch == '\r')
		return;

	if (ch == '\n')
		c->cursor += (c->w - c->cursor % c->w);

	if (c->cursor >= c->size) {
		memcpy(c->screen, c->screen + c->w, c->w * (c->h - 1));
		memset(c->screen + c->w * (c->h - 1), 0, c->w);
		c->cursor = c->w * (c->h - 1);
	}

	if (ch == '\n') {
//		vidconsole_redraw();
		return;
	}

	c->screen[c->cursor++] = ch;
}
