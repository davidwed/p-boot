// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2000-2009
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

#include <common.h>
//#include <dm.h>
//#include <errno.h>
//#include <time.h>
//#include <timer.h>
//#include <div64.h>
#include <asm/io.h>

/* Returns time in milliseconds */
static uint64_t notrace tick_to_time(uint64_t tick)
{
	ulong div = get_tbclk();

	tick *= CONFIG_SYS_HZ;
//	do_div(tick, div);
	return tick / div;
}

/* Returns time in milliseconds */
ulong __weak get_timer(ulong base)
{
	return tick_to_time(get_ticks()) - base;
}

static uint64_t notrace tick_to_time_us(uint64_t tick)
{
	ulong div = get_tbclk() / 1000;

	tick *= CONFIG_SYS_HZ;
//	do_div(tick, div);
	return tick / div;
}

uint64_t __weak get_timer_us(uint64_t base)
{
	return tick_to_time_us(get_ticks()) - base;
}

unsigned long __weak notrace timer_get_us(void)
{
	return tick_to_time(get_ticks() * 1000);
}

uint64_t usec_to_tick(unsigned long usec)
{
	uint64_t tick = usec;
	tick *= get_tbclk();
//	do_div(tick, 1000000);
	return tick / 1000000;
}

void udelay(unsigned long usec)
{
	uint64_t tmp;

	tmp = get_ticks() + usec_to_tick(usec);	/* get current timestamp */

	while (get_ticks() < tmp+1)	/* loop till event */
		 /*NOP*/;
}
