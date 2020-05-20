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
#include <asm/arch/gpio.h>
#include <asm/arch/clock.h>
#include <asm-generic/gpio.h>
#include <asm/arch/dram.h>
#include <asm/armv8/mmu.h>
#include <cpu_func.h>
#include <asm/system.h>
#include <asm/arch/clock.h>
#include <asm/arch/spl.h>
#include <asm/arch/prcm.h>
#include <arm_neon.h>
#include <build-ver.h>
#include "debug.h"
#include "mmu.h"
#include "pmic.h"
#include "lradc.h"
#include "dma.h"
#include "ce.h"

// {{{ Globals

static ulong t0;

// }}}
// {{{ LEDs control

static void green_led_set(bool on)
{
	gpio_direction_output(SUNXI_GPD(18), on); // green pinephone led
}

static void red_led_set(bool on)
{
	gpio_direction_output(SUNXI_GPD(19), on); // red on
}

// }}}
// {{{ DRAM

ulong dram_size;
static uint8_t* ps = (void*)(uintptr_t)0x40000000;
static uint8_t* pe = (void*)(uintptr_t)(0xc0000000u - 2*4096); /* last two pages contain MMU TT */

static bool dram_fill(uint32_t pat[4])
{
	uint8_t* p;

	uint32x4_t v = vld1q_u32(pat);

	for (p = ps; p < pe; p += 16)
		vst1q_u32((uint32_t*)p, v);

	flush_dcache_all();

	uint32x4_t all_eq = { 0 };
	all_eq = vmvnq_u32(all_eq);

	for (p = ps; p < pe; p += 16) {
		uint32x4_t tv = vld1q_u32((uint32_t*)p);

		uint32x4_t eq = vceqq_u32(tv, v);
		all_eq = vandq_u32(eq, all_eq);
	}

	uint32_t res[4];
	all_eq = vmvnq_u32(all_eq);
	vst1q_u32(res, all_eq);

	for (int i = 0; i < 4; i++)
		if (res[i] != 0)
			return false;
	return true;
}

static bool dram_fill_uniq(void)
{
	uint8_t* p;
	uint32_t pat[4] = {
		0, 1, 2, 3,
	};
	uint32_t pat4[4] = {
		4, 4, 4, 4,
	};
	uint32_t pat0[4] = {
		0,0,0,0
	};

	uint32x4_t v = vld1q_u32(pat);
	uint32x4_t c4 = vld1q_u32(pat4);

	for (p = ps; p < pe; p += 16) {
		vst1q_u32((uint32_t*)p, v);
		v = vaddq_u32(v, c4);
	}

	flush_dcache_all();

	uint32x4_t all_eq = vld1q_u32(pat0);
	all_eq = vmvnq_u32(all_eq);

	v = vld1q_u32(pat);
	for (p = ps; p < pe; p += 16) {
		uint32x4_t tv = vld1q_u32((uint32_t*)p);

		uint32x4_t eq = vceqq_u32(tv, v);
		all_eq = vandq_u32(eq, all_eq);

		v = vaddq_u32(v, c4);
	}

	uint32_t res[4];
	all_eq = vmvnq_u32(all_eq);
	vst1q_u32(res, all_eq);

	for (int i = 0; i < 4; i++)
		if (res[i] != 0)
			return false;

	return true;
}

static bool dram_dma_test(void)
{
	struct dma_lli d = {
		.cfg = SRC_DRQ(DRQ_SDRAM) | DST_DRQ(DRQ_SDRAM) |
			SRC_BURST(BURST_8B) | DST_BURST(BURST_8B) |
			SRC_WIDTH(WIDTH_4B) | DST_WIDTH(WIDTH_4B) |
			SRC_MODE(LINEAR_MODE) | DST_MODE(LINEAR_MODE),
		.src = 0x40000000u,
		.dst = 0x40000000u + 0x10000 * 4 + 0x8,
		.len = 0x8,
		.para = 8, // normal wait
		.p_lli_next = 0xfffff800, // last item
	};
	uint32_t* dram = (void*)(uintptr_t)0x40000000u;

	// init test data
	for (int i = 0; i < 0x20000; i++)
		dram[i] = i * 4;

	// print test data
	printf("pre DMA:\n");
	for (int i = 0; i < 0x10; i++)
		printf("%08x = %08x\n", i * 4, dram[i]);
	for (int i = 0x10000; i < 0x10000 + 0x10; i++)
		printf("%08x = %08x\n", i * 4, dram[i]);

	flush_dcache_all();

	// start
	writel(0xffffffffu, DMA_IRQ_PENDING);
	writel((uint32_t)(uintptr_t)&d, DMA_CH_DESC_ADDR(0));
	writel(0x01, DMA_CH_EN(0));
	writel(0x02, DMA_CH_PAUSE(0));

	printf("dma status = %08x\n", readl(DMA_STATUS));
	printf("dma cfg = %08x\n", readl(DMA_CH_CFG(0)));
	printf("dma cur src = %08x dst = %08x bcnt = %08x (para = %08x)\n",
	       readl(DMA_CH_SRC(0)),
	       readl(DMA_CH_DST(0)),
	       readl(DMA_CH_BCNT_LEFT(0)),
	       readl(DMA_CH_PARA(0)));

	//XXX: limit with a timer
	while (!(readl(DMA_IRQ_PENDING) & (0x4 << 0))) // wait queue end reached
		continue;

	printf("done\n");

	printf("dma cur src = %08x dst = %08x bcnt = %08x (para = %08x)\n",
	       readl(DMA_CH_SRC(0)),
	       readl(DMA_CH_DST(0)),
	       readl(DMA_CH_BCNT_LEFT(0)),
	       readl(DMA_CH_PARA(0)));

	printf("dma status = %08x\n", readl(DMA_STATUS));

	// stop
	writel(0x00, DMA_CH_EN(0));
	writel(0x00, DMA_CH_PAUSE(0));

	// print test data
	printf("post DMA:\n");
	for (int i = 0; i < 0x10; i++)
		printf("%08x = %08x\n", i * 4, dram[i]);
	for (int i = 0x10000; i < 0x10000 + 0x10; i++)
		printf("%08x = %08x\n", i * 4, dram[i]);

	return true;
}

static bool ce_test(void)
{
	struct dma_lli d = {
		.cfg = SRC_DRQ(DRQ_SDRAM) | DST_DRQ(DRQ_SDRAM) |
			SRC_BURST(BURST_8B) | DST_BURST(BURST_8B) |
			SRC_WIDTH(WIDTH_4B) | DST_WIDTH(WIDTH_4B) |
			SRC_MODE(LINEAR_MODE) | DST_MODE(LINEAR_MODE),
		.src = 0x40000000u,
		.dst = 0x40000000u + 0x10000 * 4 + 0x8,
		.len = 0x8,
		.para = 8, // normal wait
		.p_lli_next = 0xfffff800, // last item
	};
	uint32_t* dram = (void*)(uintptr_t)0x40000000u;

	ce_init();

	return true;
}

static void dram_speed_test(void)
{
	ulong t0, d;
	uint32_t fill[4];
	bool ok = true;

	flush_dcache_all();

	t0 = timer_get_boot_us();
	ok &= dram_dma_test();
	d = timer_get_boot_us() - t0;
	printf("DRAM DMA (%llu us %llu MiB/s)\n", d, 2048ull * 1000000 / d);

	return;

	memset(fill, 0, sizeof fill);
	t0 = timer_get_boot_us();
	ok &= dram_fill(fill);
	d = timer_get_boot_us() - t0;
	printf("DRAM clear (%llu us %llu MiB/s)\n", d, 2 * 2048ull * 1000000 / d);

	flush_dcache_all();

	memset(fill, 0xff, sizeof fill);
	t0 = timer_get_boot_us();
	ok &= dram_fill(fill);
	d = timer_get_boot_us() - t0;
	printf("DRAM fill (%llu us %llu MiB/s)\n", d, 2 * 2048ull * 1000000 / d);

	flush_dcache_all();

	t0 = timer_get_boot_us();
	ok &= dram_fill_uniq();
	d = timer_get_boot_us() - t0;
	printf("DRAM uniq (%llu us %llu MiB/s)\n", d, 2 * 2048ull * 1000000 / d);

	if (!ok) {
		panic(5, "FAIL!\n");
	}
}

static void dram_init(void)
{
	//dcache_disable();
	//invalidate_dcache_all();

	dram_size = sunxi_dram_init(628);
	if (!dram_size)
		panic(8, "DRAM not detected");

	icache_enable();
	mmu_setup(dram_size);

	printf("%d us: DRAM: %u MiB\n", timer_get_boot_us() - t0,
	       (unsigned)(dram_size >> 20));
}

// }}}
// {{{ SoC ID

static void get_soc_id(uint32_t sid[4])
{
	int i;
	for (i = 0; i < 4; i++)
		sid[i] = readl((ulong)SUNXI_SID_BASE + 4 * i);
}

// }}}
// {{{ Watchdog

static void soc_reset(void)
{
	/* Set the watchdog for its shortest interval (.5s) and wait */
	writel(1, (ulong)(SUNXI_TIMER_BASE + 0xB4));
	writel(1, (ulong)(SUNXI_TIMER_BASE + 0xB8));
	writel((0xa57 << 1) | 1, (ulong)(SUNXI_TIMER_BASE + 0xB0));

	udelay(1000000);
	panic(13, "SoC reset failed");
}

// }}}
// }}}

void panic_shutdown(uint32_t code)
{
	// blink green led in a binary pattern

	green_led_set(0);
	for (int i = 0; i <= 16; i++) {
		red_led_set(i % 2);
		udelay(100000);
	}

	udelay(500000);

	for (int i = 0; i < 5; i++) {
		int set = code & (1 << (4 - i));

		green_led_set(1);
		udelay(set ? 500000 : 100000);

		green_led_set(0);
		udelay(set ? 500000 : 900000);
	}

	red_led_set(1);
	udelay(500000);

	puts("Power off!\n");
	pmic_poweroff();
}

void main(void)
{
	int ret;

	t0 = timer_get_boot_us();

	green_led_set(1);
	ccu_init();
	console_init();
	lradc_enable();
	ccu_dump();

	puts("p-boot DRAM tuning program (version " VERSION " built " BUILD_DATE ")\n");

	ret = rsb_init();
	if (ret)
		panic(9, "rsb init failed %d\n", ret);

	// init PMIC first so that panic_shutdown() can power off the board
	// in case of panic()
	pmic_init();
	printf("%d us: PMIC ready\n", timer_get_boot_us() - t0);

	dram_init();
	dma_init();
	ccu_upclock();
	ccu_dump();

	dram_speed_test();

	uint32_t sid[4];
	get_soc_id(sid);
	printf("SoC ID: %08x:%08x:%08x:%08x\n", sid[0], sid[1], sid[2], sid[3]);

	// set CPU voltage to high and increase the clock to the highest OPP
	udelay(160);
	//dram_speed_test();
	clock_set_pll1(1152000000);

	udelay(2000);

	//for (int i = 0; i < 4; i++) {
		//printf("test run %d\n", i);
		//dram_speed_test();
	//}

	// mark end of p-boot by switching to red led
	green_led_set(0);
	red_led_set(1);

	puts("Reset!\n");
	soc_reset();

	panic(1, "Done");
}
