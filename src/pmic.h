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

int rsb_init(void);

int axp_write(uint8_t reg, uint8_t val);
int axp_read(uint8_t reg_addr);
int axp_clrsetbits(uint8_t reg, uint8_t clr_mask, uint8_t set_mask);

#define axp_clrbits(reg, clr_mask) axp_clrsetbits(reg, clr_mask, 0)
#define axp_setbits(reg, set_mask) axp_clrsetbits(reg, 0, set_mask)
