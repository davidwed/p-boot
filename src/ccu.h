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

#define CCU_BASE		0x01c20000ul
#define CCU_PLL_CPUX		(CCU_BASE + 0x00)
#define CCU_PLL_PERIPH0		(CCU_BASE + 0x28)
#define CCU_CPUX_AXI_CFG	(CCU_BASE + 0x50)
#define CCU_AHB1_APB1_CFG	(CCU_BASE + 0x54)
#define CCU_APB2_CFG		(CCU_BASE + 0x58)
#define CCU_AHB2_CFG		(CCU_BASE + 0x5c)
#define CCU_BUS_CLK_GATE0	(CCU_BASE + 0x60)
#define CCU_BUS_CLK_GATE3	(CCU_BASE + 0x6c)
#define CCU_CE_CLK		(CCU_BASE + 0x9c)
#define CCU_MBUS_CLK		(CCU_BASE + 0x15c)
#define CCU_BUS_SOFT_RST0	(CCU_BASE + 0x2c0)
#define CCU_BUS_SOFT_RST4	(CCU_BASE + 0x2d8)
#define CCU_PLL_LOCK_CTRL	(CCU_BASE + 0x320)

#define CCU_LOCK_MASK		(1u << 28)

void ccu_set_pll_cpux(unsigned int clk);
void ccu_init(void);
void ccu_upclock(void);

void ccu_dump(void);
