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

#include "ccu.h"
#include <asm/io.h>

#define DMA_BASE	0x01c02000ul
#define DMA_IRQ_PENDING	(DMA_BASE + 0x10)
#define DMA_AUTOGATE	(DMA_BASE + 0x28)
#define DMA_STATUS	(DMA_BASE + 0x30)

#define DMA_CH_BASE(n, off)	(DMA_BASE + 0x100 + (n) * 0x40 + (off))
#define DMA_CH_EN(n)		DMA_CH_BASE(n, 0x00)
#define DMA_CH_PAUSE(n)		DMA_CH_BASE(n, 0x04)
#define DMA_CH_DESC_ADDR(n)	DMA_CH_BASE(n, 0x08)
#define DMA_CH_CFG(n)		DMA_CH_BASE(n, 0x0c)
#define DMA_CH_SRC(n)		DMA_CH_BASE(n, 0x10)
#define DMA_CH_DST(n)		DMA_CH_BASE(n, 0x14)
#define DMA_CH_BCNT_LEFT(n)	DMA_CH_BASE(n, 0x18)
#define DMA_CH_PARA(n)		DMA_CH_BASE(n, 0x1c)
#define DMA_CH_MODE(n)		DMA_CH_BASE(n, 0x28)
#define DMA_CH_FDESC_ADDR(n)	DMA_CH_BASE(n, 0x2c)
#define DMA_CH_PKGNUM(n)	DMA_CH_BASE(n, 0x30)

struct dma_lli {
	u32 cfg;
	u32 src;
	u32 dst;
	u32 len;
	u32 para;
	u32 p_lli_next;
};

// drq
#define DRQ_SRAM	0
#define DRQ_SDRAM	1

// mode
#define LINEAR_MODE     0
#define IO_MODE         1

// data width
#define WIDTH_1B	0
#define WIDTH_2B	1
#define WIDTH_4B	2
#define WIDTH_8B	3

// burst
#define BURST_1B	0
#define BURST_4B	1
#define BURST_8B	2
#define BURST_16B	3

#define SRC_DRQ(x)     ((x) & 0x1f)
#define SRC_MODE(x)    (((x) & 0x1) << 5)
#define SRC_BURST(x)   (((x) & 0x3) << 6)
#define SRC_WIDTH(x)   (((x) & 0x3) << 9)
#define DST_DRQ(x)     (SRC_DRQ(x) << 16)
#define DST_BURST(x)   (SRC_BURST(x) << 16)
#define DST_MODE(x)    (SRC_MODE(x) << 16)
#define DST_WIDTH(x)   (SRC_WIDTH(x) << 16)

static inline void dma_init(void)
{
        // deassert reset
	writel(readl(CCU_BUS_SOFT_RST0) | BIT(6), CCU_BUS_SOFT_RST0);
	// ungate DMA clk
	writel(readl(CCU_BUS_CLK_GATE0) | BIT(6), CCU_BUS_CLK_GATE0);

	writel(0x04, DMA_AUTOGATE);
}
