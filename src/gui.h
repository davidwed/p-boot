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

enum {
	EV_VOLUME_UP,
	EV_VOLUME_DOWN,
	EV_POK_SHORT,
	EV_POK_LONG,
	EV_POK_DOWN,
	EV_VBLANK,
	EV_HEARTBEAT_1s,
	EV_HEARTBEAT_100ms,
};

struct gui {
	uint32_t events;
	uint64_t now;

	// internal
	uint64_t key_change_ts;
	int last_key;
	bool auto_repeat;

	struct display* display;

	struct gui_widget* widgets[10];
	int n_widgets;
};

struct gui_widget
{
	struct gui* gui;
	bool disabled;
	void (*update)(struct gui_widget* w);
};

void gui_init(struct gui* g, struct display* d);
void gui_get_events(struct gui* g);
void gui_fini(struct gui* g);

// menu

struct gui_menu {
	struct gui_widget widget;
	struct gui_menu_item {
		char text[128];
		uint32_t fg;
		int id;

		uint32_t active_fg;
		int active;
	} items[64];

	char titles[4][128];
	uint32_t titles_fg[4];

	int n_items;
	struct vidconsole con;
	int selection;
        int scroll_top;
        int scroll_height;
	bool changed;
	int anim_ticks;

	// events
	bool selection_changed;
};

struct gui_menu* gui_menu(struct gui* g);

enum {
	POS_TOP_LEFT,
	POS_TOP_RIGHT,
	POS_BOTTOM_LEFT,
	POS_BOTTOM_RIGHT,
};

void gui_menu_set_title(struct gui_menu* m, int pos, const char* text,
			uint32_t fg, uint32_t bg);
void gui_menu_add_item(struct gui_menu* m, int id, const char* text,
		       uint32_t fg, uint32_t active_fg);
int gui_menu_get_selection(struct gui_menu* m);
void gui_menu_set_selection(struct gui_menu* m, int id);
