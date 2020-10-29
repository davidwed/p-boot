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
#include "storage.h"
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

static const double sin_0_90[] = {
	0,
	0.017452406439562,
	0.034899496707056,
	0.052335956249771,
	0.069756473753219,
	0.087155742759009,
	0.10452846328125,
	0.12186934342098,
	0.13917310097812,
	0.15643446506049,
	0.17364817768937,
	0.19080899540115,
	0.20791169084451,
	0.22495105437273,
	0.24192189563062,
	0.25881904513554,
	0.27563735585205,
	0.29237170475979,
	0.30901699441396,
	0.3255681544981,
	0.3420201433685,
	0.35836794958998,
	0.3746065934624,
	0.39073112853752,
	0.40673664312577,
	0.42261826179233,
	0.43837114684233,
	0.45399049979437,
	0.46947156284223,
	0.48480962030414,
	0.50000000005921,
	0.51503807497061,
	0.52991926429505,
	0.5446390350781,
	0.55919290353498,
	0.57357643641638,
	0.58778525235885,
	0.60181502321939,
	0.6156614753939,
	0.62932039111891,
	0.64278760975637,
	0.65605902906102,
	0.66913060642999,
	0.68199836013417,
	0.69465837053113,
	0.70710678125906,
	0.71933980041147,
	0.73135370169222,
	0.74314482555059,
	0.75470958029603,
	0.76604444319222,
	0.77714596153011,
	0.78801075367968,
	0.79863551011998,
	0.80901699444728,
	0.81915204436088,
	0.82903757262641,
	0.83867056801617,
	0.84804809622647,
	0.85716730077136,
	0.86602540385281,
	0.87461970720679,
	0.88294759292526,
	0.89100652425355,
	0.8987940463631,
	0.90630778709925,
	0.91354545770378,
	0.9205048535121,
	0.92718385462484,
	0.93358042655355,
	0.93969262084047,
	0.94551857565199,
	0.95105651634586,
	0.95630475601167,
	0.9612616959848,
	0.96592582633331,
	0.9702957263179,
	0.97437006482471,
	0.97814760077076,
	0.98162718348202,
	0.98480775304387,
	0.98768834062401,
	0.99026806876758,
	0.99254615166437,
	0.99452189538828,
	0.99619469810863,
	0.9975640502735,
	0.99862953476495,
	0.99939082702609,
	0.99984769515993,
	1,
};

double xsin(double v)
{
	double sign = 1;

	while (v >= 180)
		v -= 180;

	if (v >= 90) {
		v -= 90;
		v = 90 - v;
		sign = -1;
	}

	double s = sin_0_90[(int)v];
	double e = sin_0_90[(int)v + 1];

	return s + sign * (e - s) * (v - (double)(int)v);
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

	struct mmc* mmc = mmc_probe(0);
	if (mmc) {
		struct bootfs* fs = bootfs_open(mmc);
		if (fs) {
			printf("OK!\n");
			bootfs_load_file(fs, 0x48000000, "pboot2.argb");
			bootfs_load_file(fs, 0x49000000, "off.argb");
		}
	}

	ccu_upclock();
	udelay(160);
	clock_set_pll1(1152000000);

	sys_console = zalloc(sizeof *sys_console);
	vidconsole_init(sys_console, 45, 45, 2, 0xffffeecc, 0xff000000);

	puts("p-boot display program\n (version " VERSION "\n built " BUILD_DATE ")\n");

	// 45x45 or 90x90
	struct vidconsole* tconsole = zalloc(sizeof *tconsole);
	vidconsole_init(tconsole, 45, 45, 2, 0xffffeecc, 0xff104010);
	for (unsigned x = 0; x < 45; x++)
		for (unsigned y = 0; y < 45; y++)
			vidconsole_set_xy(tconsole, x, y, 'q', 0xffffff33, 0);
	vidconsole_redraw(tconsole);

	display_init();

	vidconsole_redraw(sys_console);
	udelay(160000);
	backlight_enable(90);

	uint32_t* fb = malloc(4 * 600 * 600);
	for (int i = 0; i < 600 * 600; i++)
		fb[i] = 0xff000000 | (i * 3);

	struct display* d = zalloc(sizeof *d);
	struct gui* g = zalloc(sizeof *g);
        gui_init(g, d);

	d->planes[0].fb_start = sys_console->fb_start;
	d->planes[0].fb_pitch = 720 * 4;
	d->planes[0].src_w = 720;
	d->planes[0].src_h = 1440;
	d->planes[0].dst_w = 720;
	d->planes[0].dst_h = 1440;

	d->planes[1].fb_start = fb;
	d->planes[1].fb_start = 0;
	d->planes[1].fb_pitch = 600 * 4;
	d->planes[1].src_w = 600;
	d->planes[1].src_h = 600;
	d->planes[1].dst_w = 600;
	d->planes[1].dst_h = 600;
	d->planes[1].dst_x = 52;
	d->planes[1].dst_y = 52;

	d->planes[2].fb_start = tconsole->fb_start;
	d->planes[2].fb_start = 0x48000000;
	//d->planes[2].fb_start = 0;
	d->planes[2].fb_pitch = 720 * 4;
	d->planes[2].src_w = 720;
	d->planes[2].src_h = 1440;
	d->planes[2].dst_w = 720;
	d->planes[2].dst_h = 1440;
	d->planes[2].dst_x = 0;
	d->planes[2].dst_y = 0;
	d->planes[2].alpha = 255;

	display_commit(d);

#define FRAMES 60

	ulong t0;
	int frames = 0;
	while (1) {
		gui_get_events(g);
		if (g->events & BIT(EV_VBLANK)) {
			//d->planes[1].dst_x = 720/2 - 300 + xsin(frames * 2) * 720 / 2;
			//d->planes[1].dst_y = 1440/2 - 300 + xsin(90 + frames * 2) * 1440 / 2;

			double pct = xsin(((double)(frames)  / FRAMES * 2) * 180);
			d->planes[2].dst_h = 1440.0 * (2 - pct);
			d->planes[2].dst_w = 720.0 * (2 - pct);
			d->planes[2].alpha = (1 - pct * 1) * 255;
			d->planes[2].dst_y = -1440.0 * (1 - pct) / 2;
			d->planes[2].dst_x = -1440.0 * (1 - pct) / 4;

			display_commit(g->display);

			frames++;
			if (frames == 1) {
				t0 = timer_get_boot_us();
			} else if (frames == FRAMES + 1) {
				printf("Framerate: %lld mHz\n", 1ull*FRAMES*1000*1000*1000 / (timer_get_boot_us() - t0));
				break;
			}
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
