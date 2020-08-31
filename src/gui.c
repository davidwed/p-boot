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
#include <asm/system.h>
#include <asm/io.h>
#include <cpu_func.h>
#include "pmic.h"
#include "lradc.h"
#include "display.h"
#include "vidconsole.h"
#include "gui.h"

// enable r_intc to get access to PMIC interrupt
#define R_INTC_BASE 0x1f00c00ul
#define SUN6I_R_INTC_CTRL       0x0c
#define SUN6I_R_INTC_PENDING    0x10
#define SUN6I_R_INTC_ENABLE     0x40
#define SUN6I_R_INTC_MASK       0x50

enum {
        SUNXI_SRC_TYPE_LEVEL_LOW = 0,
        SUNXI_SRC_TYPE_EDGE_FALLING,
        SUNXI_SRC_TYPE_LEVEL_HIGH,
        SUNXI_SRC_TYPE_EDGE_RISING,
};

void gui_init(struct gui* gui, struct display* d)
{
	writel(SUNXI_SRC_TYPE_LEVEL_LOW, R_INTC_BASE + SUN6I_R_INTC_CTRL);
	writel(BIT(0), R_INTC_BASE + SUN6I_R_INTC_MASK);
	writel(BIT(0), R_INTC_BASE + SUN6I_R_INTC_ENABLE);
	writel(BIT(0), R_INTC_BASE + SUN6I_R_INTC_PENDING);

	gui->now = timer_get_boot_us();
	gui->last_key = lradc_get_pressed_key();

	gui->display = d;
}

void gui_get_events(struct gui* gui)
{
	uint64_t now = timer_get_boot_us();

	gui->events = 0;

	if (display_frame_done())
		gui->events |= BIT(EV_VBLANK);

	// PMIC asking us to handle interrupt
	if (readl(R_INTC_BASE + SUN6I_R_INTC_PENDING) & BIT(0)) {
		// clear PMIC interrupt flags
		for (int i = 0x48; i <= 0x4d; i++) {
			unsigned f = pmic_read(i);
			if (!f)
				continue;

			if (i == 0x4c) {
				// read POK interrupt flags
				if (f & BIT(4))
					gui->events |= BIT(EV_POK_SHORT);
				if (f & BIT(3))
					gui->events |= BIT(EV_POK_LONG);
				if (f & BIT(5))
					gui->events |= BIT(EV_POK_DOWN);
			}

			pmic_write(i, f);
		}

		writel(BIT(0), R_INTC_BASE + SUN6I_R_INTC_PENDING);
	}

	int key = lradc_get_pressed_key();
	if (gui->last_key != key) {
		gui->last_key = key;
		gui->key_change_ts = timer_get_boot_us();
	}

	if (gui->key_change_ts && gui->key_change_ts + 50000 < now) {
		gui->key_change_ts = 0;

		if (key == KEY_VOLUMEDOWN) {
			gui->events |= BIT(EV_VOLUME_DOWN);
		} else if (key == KEY_VOLUMEUP) {
			gui->events |= BIT(EV_VOLUME_UP);
		}
	}

	gui->now = now;

	for (int i = 0; i < gui->n_widgets; i++)
		if (!gui->widgets[i]->disabled)
			gui->widgets[i]->update(gui->widgets[i]);
}

void gui_fini(struct gui* gui)
{
	writel(0, R_INTC_BASE + SUN6I_R_INTC_MASK);
	writel(0, R_INTC_BASE + SUN6I_R_INTC_ENABLE);
	writel(BIT(0), R_INTC_BASE + SUN6I_R_INTC_PENDING);
}

// menu

void gui_menu_update(struct gui_widget* w)
{
	struct gui_menu* m = container_of(w, struct gui_menu, widget);
	struct display* d = w->gui->display;
	struct vidconsole* c = &m->con;

	m->selection_changed = false;

	if (m->n_items > 0) {
		if (w->gui->events & BIT(EV_VOLUME_DOWN)) {
			for (int i = m->selection + 1; i < m->n_items; i++) {
				if (m->items[i].text[0]) {
					m->selection = i;
					m->changed = true;
					m->selection_changed = true;
					break;
				}
			}
		}

		if (w->gui->events & BIT(EV_VOLUME_UP)) {
			for (int i = m->selection - 1; i >= 0; i--) {
				if (m->items[i].text[0]) {
					m->selection = i;
					m->changed = true;
					m->selection_changed = true;
					break;
				}
			}
		}
	}

	if (m->changed) {
		int pad = 1;
		unsigned w = c->w;
		unsigned h = c->h;

		// draw the menu
		for (unsigned y = 0; y < h; y++) {
			bool top = y == 0;
			bool bottom = y == h - 1;

                        char* line_text = NULL;
			int line_text_len = 0;
                        bool line_sel = false;
			uint32_t line_fg;

                        char* lh_text = NULL;
			int lh_text_len;
			uint32_t lh_text_fg;

                        char* rh_text = NULL;
			int rh_text_len;

			if (top && m->titles[POS_TOP_LEFT]) {
				lh_text = m->titles[POS_TOP_LEFT];
				lh_text_len = strlen(m->titles[POS_TOP_LEFT]);
				lh_text_fg = m->titles_fg[POS_TOP_LEFT];
			}

			if (bottom && m->titles[POS_BOTTOM_LEFT]) {
				lh_text = m->titles[POS_BOTTOM_LEFT];
				lh_text_len = strlen(m->titles[POS_BOTTOM_LEFT]);
				lh_text_fg = m->titles_fg[POS_BOTTOM_LEFT];
			}

			// determine item on this line
			if (y >= pad + 1 && y < h - pad - 1) {
				int i = y - pad - 1 + m->scroll_top;
				if (i < m->n_items) {
					struct gui_menu_item* it = &m->items[i];

					if (it->text[0]) {
						line_text_len = strlen(it->text);
						line_text = it->text;
						line_sel = i == m->selection;
						line_fg = it->fg;
					}
				}
			}

			for (unsigned x = 0; x < w; x++) {
				char ch = ' ';
				bool left = x == 0;
				bool right = x == w - 1;
				uint32_t bg = 0xaa000000;
				uint32_t fg = 0xff00ff00;

				if (left && top)
					ch = '/';
				else if (left && bottom)
					ch = '\\';
				else if (right && top)
					ch = '\\';
				else if (right && bottom)
					ch = '/';
				else if (left || right)
					ch = '|';
				else if (top || bottom)
					ch = '=';

				if (lh_text) {
					if (x == 2) {
						ch = '[';
					} else if (x >= 3 && x < lh_text_len + 3) {
						ch = lh_text[x - 3];
						fg = lh_text_fg;
					} else if (x == lh_text_len + 3) {
						ch = ']';
					}
				}

				if (line_text) {
					if (x >= 1 + pad && x < w - 1 - pad && x - 1 - pad < line_text_len) {
						ch = line_text[x - 1 - pad];
						if (line_sel) {
							bg = line_fg;
							fg = 0xaa000000;
						} else {
							fg = line_fg;
						}
					}
				}

				vidconsole_set_xy(c, x, y, ch, fg, bg);
			}
		}

		// render items
		vidconsole_redraw(c);
		m->changed = false;
	}

	if (w->gui->events & BIT(EV_VBLANK)) {
		int alpha = 0;
		int dst_y = (1440 - c->fb_height - d->planes[1].dst_x);
		int dur = 10;
		if (m->anim_ticks <= dur) {
			alpha = 255 - 255 * m->anim_ticks / dur;
			dst_y -= 20 * (dur - m->anim_ticks) / dur;
			m->anim_ticks++;
		}

		d->planes[1].fb_start = c->fb_start;
		d->planes[1].fb_pitch = c->fb_pitch;
		d->planes[1].src_w = c->fb_width;
		d->planes[1].src_h = c->fb_height;
		d->planes[1].dst_w = c->fb_width;
		d->planes[1].dst_h = c->fb_height;
		d->planes[1].dst_x = (720 - c->fb_width) / 2;
		d->planes[1].dst_y = dst_y;
		d->planes[1].alpha = alpha;
	}
}

struct gui_menu* gui_menu(struct gui* g)
{
	struct gui_menu* m = zalloc(sizeof *m);
	unsigned w = 40, h = 21;
	struct vidconsole* c = &m->con;

	m->widget.gui = g;
	m->widget.update = gui_menu_update;
	m->changed = true;

	g->widgets[g->n_widgets++] = &m->widget;

	vidconsole_init(c, w, h, 2, 0xff112233, 0xcc000000);

	return m;
}

void gui_menu_set_title(struct gui_menu* m, int pos, const char* text,
			uint32_t fg, uint32_t bg)
{
	m->changed = true;

	if (text) {
		memcpy(m->titles[pos], text, strlen(text) + 1);
		m->titles_fg[pos] = fg;
	} else
		m->titles[pos][0] = 0;
}

void gui_menu_add_item(struct gui_menu* m, int id, const char* text,
		       uint32_t fg)
{
	m->changed = true;

	memcpy(m->items[m->n_items].text, text, strlen(text) + 1);
	m->items[m->n_items].fg = fg;
	m->items[m->n_items++].id = id;
}

int gui_menu_get_selection(struct gui_menu* m)
{
	return m->items[m->selection].id;
}

void gui_menu_set_selection(struct gui_menu* m, int id)
{
	for (int i = 0; i < m->n_items; i++) {
		if (m->items[i].id == id) {
			m->selection = i;
			m->changed = true;
			break;
		}
	}
}
