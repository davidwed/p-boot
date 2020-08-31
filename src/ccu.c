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

#include "ccu.h"
#include <asm/io.h>
#include <asm/arch/prcm.h>

void ccu_set_pll_cpux(unsigned int clk)
{
	/*
	 * rate = (24MHz * N * K) / (M * 2^P)
	 */
	unsigned k = 1;
	if (clk >= 768000000)
		k = 2;

	unsigned n = clk / (24000000 * k);

	writel((1 << 16 /* cpux_src: 1=24mhz 2=PLL_CPUX */) |
	       (1 << 8 /* apb_div: 1=/2 */) |
	       (2 << 0 /* axi_div: 2=/3 1=/2 0=/1 */),
	       CCU_CPUX_AXI_CFG);

	udelay(2);

	writel((1u << 31 /* enable */) |
	       (0 << 16 /* P: 0=/1 1=/2 2=/4 */) |
	       ((n-1) << 8 /* N-1 (0-31) */) |
	       ((k-1) << 4 /* K-1 (0-3) */) |
	       (0 << 0 /* M-1 (0-3) */),
	       CCU_PLL_CPUX);

	while (!(readl(CCU_PLL_CPUX) & CCU_LOCK_MASK));

	writel((2 << 16 /* cpux_src: 1=24mhz 2=PLL_CPUX */) |
	       (1 << 8 /* apb_div: 1=/2 */) |
	       (2 << 0 /* axi_div: 2=/3 1=/2 0=/1 */),
	       CCU_CPUX_AXI_CFG);

	udelay(2);
}

void ccu_init(void)
{
	/* Set PLL lock enable bits and switch to old lock mode */
	writel(0x1fff, CCU_PLL_LOCK_CTRL);

	ccu_set_pll_cpux(408000000);

	/* PLL_PERIPH0(2x) = 24MHz * N * K, reset to default  */
	writel(0x80041811 /* 600 MHz */, CCU_PLL_PERIPH0);

	while (!(readl(CCU_PLL_PERIPH0) & CCU_LOCK_MASK));

	/*
	 * AHB1 = PLL_PERIPH0(x1) / 6
	 * APB1 = AHB1 / 2
	 */
	writel((3 << 12 /* ahb1 src: 3 = pll_periph0(x1) / ahb1_pre_div */) |
		(1 << 8 /* ahb1 clk ratio: 1 = /2 */) |
		(2 << 6 /* ahb1_pre_div: 2 = /3 */) |
		(1 << 4 /* ahb1_clk_div_ratio: 1 = /2 */),
		CCU_AHB1_APB1_CFG);

	/* 1200MHz / 3 = 400MHz */
	writel((1u << 31 /* enable */) |
	       (1 << 24 /* src: 1 = PLL_PERIPH0(2x) */) |
	       (2 << 0 /* M: 2 = /3 */),
	       CCU_MBUS_CLK);

	/* APB2 = 24MHz (UART, I2C) */
	writel((1 << 24 /* src: 1=osc24 */) |
	       (0 << 16 /* pre_div (N): 0=/1 1=/2 2=/4 3=/8 */) |
	       (0 << 0) /* M-1 */,
	       CCU_APB2_CFG);
}

void ccu_upclock(void)
{
	ccu_set_pll_cpux(816000000);

	writel((3 << 12 /* ahb1 src: 3 = pll_periph0(x1) / ahb1_pre_div */) |
		(1 << 8 /* ahb1 clk ratio: 1 = /2 */) |
		(2 << 6 /* ahb1_pre_div: 2 = /3 */) |
		(0 << 4 /* ahb1_clk_div_ratio: 0 = /1 .. 3 = /8 */),
		CCU_AHB1_APB1_CFG);

	writel((1 << 0 /* ahb2 source: 0=ahb1 1=pll_periph0(1x)/2 */), CCU_AHB2_CFG);
}

void ccu_dump(void)
{
	unsigned long regs[] = {
		CCU_CE_CLK,
		CCU_BUS_CLK_GATE0,
		CCU_BUS_SOFT_RST0,
		CCU_CPUX_AXI_CFG,
		CCU_PLL_CPUX,
		CCU_PLL_LOCK_CTRL,
		CCU_PLL_PERIPH0,
		CCU_AHB1_APB1_CFG,
		CCU_MBUS_CLK,
		CCU_AHB2_CFG,
	};

	for (unsigned i = 0; i < ARRAY_SIZE(regs); i++)
		printf("%08x %08lx\n", regs[i], readl(regs[i]));
}
