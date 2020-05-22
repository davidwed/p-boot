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

#define START 0x40000000u
//#define END 0x50000000u
#define END (0xc0000000u - 2*4096)

static uint8_t* ps = (void*)(uintptr_t)START;
static uint8_t* pe = (void*)(uintptr_t)(END); /* last two pages contain MMU TT */
static uint64_t test_size = (END - START) / 1024 / 1024;

static inline uint32_t* shuffle_ptr(uint8_t* p)
{
	// test routines work with 16B block, so we need to take
	// care of making sure shuffled pointer will not overlap
	// the blocks or leave the DRAM address range
	
	// aabbccdd 0x40123456
	uint32_t a = (uint32_t)(uintptr_t)p;
	// ccbb 0x3412
	uint32_t b = __builtin_bswap16(a >> 8);
	// aaccbbdd 0x40341256
	uint32_t c = (a & 0xff0000ff) | (b << 8);

	/* last two pages contain MMU TT, avoid those addresses */
	if (c >= (0xc0000000u - 2*4096) || a >= (0xc0000000u - 2*4096))
		return (uint32_t*)(uintptr_t)a;

	return (uint32_t*)(uintptr_t)c;
}

static bool dram_fill(uint32_t pat[4], bool verify, bool raccess)
{
	uint8_t* p;

	uint32x4_t v = vld1q_u32(pat);

	if (raccess) {
		for (p = ps; p < pe; p += 16)
			vst1q_u32(shuffle_ptr(p), v);
	} else {
		for (p = ps; p < pe; p += 16)
			vst1q_u32((uint32_t*)p, v);
	}

	flush_dcache_all();

	if (!verify)
		return true;

	uint32x4_t all_eq = { 0 };
	all_eq = vmvnq_u32(all_eq);

	if (raccess) {
		for (p = ps; p < pe; p += 16) {
			uint32x4_t tv = vld1q_u32(shuffle_ptr(p));

			uint32x4_t eq = vceqq_u32(tv, v);
			all_eq = vandq_u32(eq, all_eq);
		}
	} else {
		for (p = ps; p < pe; p += 16) {
			uint32x4_t tv = vld1q_u32((uint32_t*)p);

			uint32x4_t eq = vceqq_u32(tv, v);
			all_eq = vandq_u32(eq, all_eq);
		}
	}

	uint32_t res[4];
	all_eq = vmvnq_u32(all_eq);
	vst1q_u32(res, all_eq);

	for (int i = 0; i < 4; i++)
		if (res[i] != 0)
			return false;
	return true;
}

static bool dram_fill_uniq(uint32_t off, bool verify, bool raccess)
{
	uint8_t* p;
	uint32_t pat[4] = {
		off + 0, off + 4, off + 8, off + 12,
	};
	uint32_t pat4[4] = {
		16, 16, 16, 16,
	};
	uint32_t pat0[4] = {
		0, 0, 0, 0
	};

	uint32x4_t v = vld1q_u32(pat);
	uint32x4_t c4 = vld1q_u32(pat4);

	if (raccess) {
		for (p = ps; p < pe; p += 16) {
			vst1q_u32(shuffle_ptr(p), v);
			v = vaddq_u32(v, c4);
		}
	} else {
		for (p = ps; p < pe; p += 16) {
			vst1q_u32((uint32_t*)p, v);
			v = vaddq_u32(v, c4);
		}
	}

	flush_dcache_all();

	if (!verify)
		return true;

	uint32x4_t all_eq = vld1q_u32(pat0);
	all_eq = vmvnq_u32(all_eq);

	v = vld1q_u32(pat);

	if (raccess) {
		for (p = ps; p < pe; p += 16) {
			uint32x4_t tv = vld1q_u32(shuffle_ptr(p));

			uint32x4_t eq = vceqq_u32(tv, v);
			all_eq = vandq_u32(eq, all_eq);

			v = vaddq_u32(v, c4);
		}
	} else {
		for (p = ps; p < pe; p += 16) {
			uint32x4_t tv = vld1q_u32((uint32_t*)p);

			uint32x4_t eq = vceqq_u32(tv, v);
			all_eq = vandq_u32(eq, all_eq);

			v = vaddq_u32(v, c4);
		}
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
	struct dma_lli tpl = {
		.cfg = SRC_DRQ(DRQ_SDRAM) | DST_DRQ(DRQ_SDRAM) |
			SRC_BURST(BURST_16B) | DST_BURST(BURST_16B) |
			SRC_WIDTH(WIDTH_4B) | DST_WIDTH(WIDTH_4B) |
			SRC_MODE(LINEAR_MODE) | DST_MODE(LINEAR_MODE),
		.src = 0x40000000u,
		.dst = 0x80000000u,
		.len = 0x1ffe000,
		.para = 8, // normal wait
		.p_lli_next = 0xfffff800, // last item
	};
	struct dma_lli d[40];
	uint32_t total = 0x40000000u - 2 * 4096;

	for (int i = 0; i < ARRAY_SIZE(d); i++) {
		memcpy(&d[i], &tpl, sizeof tpl);
		if (i > 0)
			d[i - 1].p_lli_next = (uint32_t)(uintptr_t)&d[i];

		d[i].src += d[i].len * i;
		d[i].dst += d[i].len * i;

		if (total > tpl.len) {
			total -= tpl.len;
		} else {
			d[i].len = total;
			break;
		}
	}

#if 0
	uint32_t* dram = (void*)(uintptr_t)0x40000000u;

	printf("pre DMA:\n");
	for (unsigned i = 0x40000000u; i < 0x40000000u + 0x10; i+=4)
		printf("%08x = %08x\n", i, dram[i / 4 - 0x10000000u]);
	for (unsigned i = 0x80000000u; i < 0x80000000u + 0x10; i+=4)
		printf("%08x = %08x\n", i, dram[i / 4 - 0x10000000u]);
	for (unsigned i = 0x80000000u - 2*4096 - 0x10; i < 0x80000000u - 2*4096; i+=4)
		printf("%08x = %08x\n", i, dram[i / 4 - 0x10000000u]);
	for (unsigned i = 0xc0000000u - 2*4096 - 0x10; i < 0xc0000000u - 2*4096; i+=4)
		printf("%08x = %08x\n", i, dram[i / 4 - 0x10000000u]);
#endif
#if 1
	flush_dcache_all();

	// start
	writel(0xffffffffu, DMA_IRQ_PENDING);
	writel((uint32_t)(uintptr_t)&d, DMA_CH_DESC_ADDR(0));
	writel(0x01, DMA_CH_EN(0));
	writel(0x02, DMA_CH_PAUSE(0));

	/*
	printf("dma status = %08x\n", readl(DMA_STATUS));
	printf("dma cfg = %08x\n", readl(DMA_CH_CFG(0)));
	printf("dma cur src = %08x dst = %08x bcnt = %08x (para = %08x)\n",
	       readl(DMA_CH_SRC(0)),
	       readl(DMA_CH_DST(0)),
	       readl(DMA_CH_BCNT_LEFT(0)),
	       readl(DMA_CH_PARA(0)));
          */
	//XXX: limit with a timer
	while (!(readl(DMA_IRQ_PENDING) & (0x4 << 0))) // wait queue end reached
		continue;
            /*
	printf("done\n");

	printf("dma cur src = %08x dst = %08x bcnt = %08x (para = %08x)\n",
	       readl(DMA_CH_SRC(0)),
	       readl(DMA_CH_DST(0)),
	       readl(DMA_CH_BCNT_LEFT(0)),
	       readl(DMA_CH_PARA(0)));
              */
	//printf("dma status = %08x\n", readl(DMA_STATUS));

	// stop
	writel(0x00, DMA_CH_EN(0));
	writel(0x00, DMA_CH_PAUSE(0));
#endif

#if 0
	printf("post DMA:\n");
	for (unsigned i = 0x80000000u; i < 0xc0000000u - 2 * 4096; i+=4) {
		if (dram[i / 4 - 0x10000000u] != i - 0x40000000u) {
			printf("mismatch at %08x = %08x\n", i, dram[i / 4 - 0x10000000u]);
			break;
		}
	}

	for (unsigned i = 0x40000000u; i < 0x40000000u + 0x10; i+=4)
		printf("%08x = %08x\n", i, dram[i / 4 - 0x10000000u]);
	for (unsigned i = 0x80000000u; i < 0x80000000u + 0x10; i+=4)
		printf("%08x = %08x\n", i, dram[i / 4 - 0x10000000u]);
	for (unsigned i = 0x80000000u - 2*4096 - 0x10; i < 0x80000000u - 2*4096; i+=4)
		printf("%08x = %08x\n", i, dram[i / 4 - 0x10000000u]);
	for (unsigned i = 0xc0000000u - 2*4096 - 0x10; i < 0xc0000000u - 2*4096; i+=4)
		printf("%08x = %08x\n", i, dram[i / 4 - 0x10000000u]);
#endif
	return true;
}

static bool ce_test(void)
{
#if 0
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
#endif
	return true;
}

static bool dram_speed_test(bool raccess)
{
	ulong t0, d;
	uint32_t fill[4];
	bool ok, tot_ok = true;

	flush_dcache_all();

	t0 = timer_get_boot_us();
	memset(fill, 0, sizeof fill);
	ok = dram_fill(fill, true, raccess);
	flush_dcache_all();
	d = timer_get_boot_us() - t0;
	printf("DRAM fill 0x00000000 (%llu us %llu MiB/s) %s\n", d, 2 * test_size * 1000000 / d, ok ? "OK" : "FAIL");
	
	tot_ok &= ok;

	t0 = timer_get_boot_us();
	memset(fill, 0xff, sizeof fill);
	ok = dram_fill(fill, true, raccess);
	flush_dcache_all();
	d = timer_get_boot_us() - t0;
	printf("DRAM fill 0xffffffff (%llu us %llu MiB/s) %s\n", d, 2 * test_size * 1000000 / d, ok ? "OK" : "FAIL");

	tot_ok &= ok;

	t0 = timer_get_boot_us();
	memset(fill, 0, sizeof fill);
	ok = dram_fill(fill, true, raccess);
	flush_dcache_all();
	d = timer_get_boot_us() - t0;
	printf("DRAM fill 0x00000000 (%llu us %llu MiB/s) %s\n", d, 2 * test_size * 1000000 / d, ok ? "OK" : "FAIL");

	tot_ok &= ok;

	// fill dram with a sequence of 32bit numbers
	t0 = timer_get_boot_us();
	ok = dram_fill_uniq(0x40000000u, true, raccess);
	flush_dcache_all();
	d = timer_get_boot_us() - t0;
	printf("DRAM fill [addr]     (%llu us %llu MiB/s) %s\n", d, 2 * test_size * 1000000 / d, ok ? "OK" : "FAIL");

	tot_ok &= ok;
/*
	// copy first 1GiB to the second one via DMA
	t0 = timer_get_boot_us();
	ok = dram_dma_test();
	d = timer_get_boot_us() - t0;
	printf("DRAM DMA test        (%llu us %llu MiB/s)\n", d, 1024ull * 1000000 / d, ok ? "OK" : "FAIL");
	tot_ok &= ok;
*/

	return tot_ok;
}

static void dram_init(unsigned freq)
{
	dcache_disable();
	invalidate_dcache_all();

	dram_size = sunxi_dram_init(freq);
	if (!dram_size)
		panic(8, "DRAM not detected");

	icache_enable();
	mmu_setup(dram_size);

	printf("%d us: DRAM: %u MiB at %u MHz\n", timer_get_boot_us() - t0,
	       (unsigned)(dram_size >> 20), freq);
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
	uint32_t sid[4];

	t0 = timer_get_boot_us();

	green_led_set(1);
	ccu_init();
	console_init();
	lradc_enable();
	get_soc_id(sid);

	puts("p-boot DRAM tuning program (version " VERSION " built " BUILD_DATE ")\n");
	printf("SoC ID: %08x:%08x:%08x:%08x\n", sid[0], sid[1], sid[2], sid[3]);

	ret = rsb_init();
	if (ret)
		panic(9, "rsb init failed %d\n", ret);

	// init PMIC first so that panic_shutdown() can power off the board
	// in case of panic()
	pmic_init();
	udelay(500);
	printf("%d us: PMIC ready\n", timer_get_boot_us() - t0);

	dma_init();

//	for (unsigned ahb = 1; ahb <= 4; ahb++) {
	for (unsigned f = 528; f <= 672; f += 24) {
		dram_init(f);

		writel((3 << 12 /* ahb1 src: 3 = pll_periph0(x1) / ahb1_pre_div */) |
			(3 << 8 /* apb1 clk ratio: 1 = /2 .. 3 = /8 */) |
			(2 << 6 /* ahb1_pre_div: 1=/2 2=/3 3=/4 */) |
			(0 << 4 /* ahb1_clk_div_ratio: 0=/1 .. 3=/8 */),
			CCU_AHB1_APB1_CFG);
		udelay(10);
		clock_set_pll1(1152000000);
		udelay(10);

		printf("\nsequential:\n\n");
		dram_speed_test(false);
		printf("\nscattered:\n\n");
		dram_speed_test(true);

		writel((3 << 12 /* ahb1 src: 3 = pll_periph0(x1) / ahb1_pre_div */) |
			(1 << 8 /* ahb1 clk ratio: 1 = /2 */) |
			(2 << 6 /* ahb1_pre_div: 2 = /3 */) |
			(0 << 4 /* ahb1_clk_div_ratio: 0 = /1 .. 3 = /8 */),
			CCU_AHB1_APB1_CFG);

		udelay(10);
		ccu_set_pll_cpux(408000000);
		udelay(10);
	}
//	}

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
