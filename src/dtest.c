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
#include "display.h"
#include "vidconsole.h"
#include "gui.h"

static uint8_t* heap_end = (uint8_t*)(uintptr_t)0x40000000u;

void* malloc(size_t len)
{
	void* p = heap_end;
	heap_end += ALIGN(len, 64);
	return p;
}

void* zalloc(size_t len)
{
	void* p = malloc(len);
	memset(p, 0, len);
	return p;
}

void free(void* p)
{
}

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

// timeout is 0 - 11, 0 = 0.5s, 1 = 1s ... 6 = 6s, 7 = 8s ... 11 = 16s
static void wdog_ping(uint32_t timeout)
{
	writel(1, (ulong)(SUNXI_TIMER_BASE + 0xB4));
	writel(0x1 | (timeout << 4), (ulong)(SUNXI_TIMER_BASE + 0xB8));
	writel((0xa57 << 1) | 1, (ulong)(SUNXI_TIMER_BASE + 0xB0));
}

// }}}

void panic_shutdown(uint32_t code)
{
	green_led_set(0);
	red_led_set(1);

	puts("Power off!\n");
	pmic_poweroff();
}

void main(void)
{
	int ret;
	ulong dram_size;

	green_led_set(1);
	ccu_init();
	console_init();
	lradc_enable();

	wdog_ping(8); // 10s

	puts("p-boot display program (version " VERSION " built " BUILD_DATE ")\n");

	ret = rsb_init();
	if (ret)
		panic(9, "rsb init failed %d\n", ret);

	pmic_init();
	udelay(500);

	dram_size = sunxi_dram_init();
	if (!dram_size)
		panic(3, "DRAM not detected");

	icache_enable();
	mmu_setup(dram_size);

	ccu_upclock();
	udelay(160);
	clock_set_pll1(1152000000);

	sys_console = malloc(sizeof *sys_console);
	vidconsole_init(sys_console, 45, 45, 2, 0xffffeecc, 0xff102030);

	puts("p-boot display program\n (version " VERSION "\n built " BUILD_DATE ")\n");

	display_init();

	// 45x45 or 90x90
//	for (unsigned x = 0; x < 45; x++)
//		for (unsigned y = 0; y < 45; y++)
//			vidconsole_set_xy(sys_console, x, y, '*', 0x00ff1133, 0);

	vidconsole_redraw(sys_console);
	udelay(160000);
	backlight_enable(70);

	struct display* d = zalloc(sizeof *d);
	struct gui* g = zalloc(sizeof *g);
        gui_init(g, d);
	d->planes[0].fb_start = sys_console->fb_start;
	d->planes[0].fb_pitch = 720 * 4;
	d->planes[0].src_w = 720;
	d->planes[0].src_h = 1440;
	d->planes[0].dst_w = 720;
	d->planes[0].dst_h = 1440;
	d->planes[1].fb_start = 0x48000000;
	d->planes[1].fb_start = 0;
	d->planes[1].fb_pitch = 720 * 4;
	d->planes[1].src_w = 600;
	d->planes[1].src_h = 600;
	d->planes[1].dst_w = 600;
	d->planes[1].dst_h = 600;
	d->planes[1].dst_x = 52;
	d->planes[1].dst_y = 52;
	display_commit(d);

	ulong t0;
	int frames = 0;
	while (1) {
		gui_get_events(g);
		if (g->events & BIT(EV_VBLANK)) {
			frames++;
			if (frames == 1) {
				t0 = timer_get_boot_us();
			} else if (frames == 121) {
				printf("Framerate: %lld mHz\n", 120ull*1000*1000*1000 / (timer_get_boot_us() - t0));
				break;
			}

			display_commit(g->display);
		}
	}

	vidconsole_redraw(sys_console);

	// mark end of p-boot by switching to red led
	green_led_set(0);
	red_led_set(1);

	udelay(2000000);

	puts("Reset!\n");
	soc_reset();

	panic(1, "Done");
}
