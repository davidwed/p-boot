/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Common header file for U-Boot
 *
 * This file still includes quite a bit of stuff that should be in separate
 * headers. Please think before adding more things.
 * Patches to remove things are welcome.
 *
 * (C) Copyright 2000-2009
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

#ifndef __COMMON_H_
#define __COMMON_H_	1

#ifndef __ASSEMBLY__		/* put C only stuff in this section */

typedef volatile unsigned long	vu_long;
typedef volatile unsigned short vu_short;
typedef volatile unsigned char	vu_char;

//#include <config.h>
#include <linux/kconfig.h>
#include <linux/errno.h>
#include <time.h>
//#include <asm-offsets.h>
#include <linux/bitops.h>
//#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/types.h>
//#include <linux/printk.h>
#include <linux/string.h>
#include <linux/stringify.h>
#include <asm/ptrace.h>
#include <stdarg.h>
#include <linux/kernel.h>

#ifdef __LP64__
#define CONFIG_SYS_SUPPORT_64BIT_DATA
#endif

//#include <asm/u-boot.h> /* boot information for Linux kernel */

#define ENOTSUPP	524

#define LOG2(x) (((x & 0xaaaaaaaa) ? 1 : 0) + ((x & 0xcccccccc) ? 2 : 0) + \
		 ((x & 0xf0f0f0f0) ? 4 : 0) + ((x & 0xff00ff00) ? 8 : 0) + \
		 ((x & 0xffff0000) ? 16 : 0))
#define LOG2_INVALID(type) ((type)((sizeof(type)<<3)-1))

ulong	get_tbclk     (void);
ulong timer_get_boot_us(void);

#include <debug.h>

struct globals {
	struct {
		unsigned long tlb_addr;
		unsigned long tlb_size;
		unsigned long tlb_fillptr;
		unsigned long tlb_emerg;
	} arch;
};

#define DECLARE_GLOBAL_DATA_PTR \
	extern struct globals* gd

#endif	/* __ASSEMBLY__ */

#define ROUND(a,b)		(((a) + (b) - 1) & ~((b) - 1))

/*
 * check_member() - Check the offset of a structure member
 *
 * @structure:	Name of structure (e.g. global_data)
 * @member:	Name of member (e.g. baudrate)
 * @offset:	Expected offset in bytes
 */
#define check_member(structure, member, offset) _Static_assert( \
	offsetof(struct structure, member) == offset, \
	"`struct " #structure "` offset for `" #member "` is not " #offset)

#endif	/* __COMMON_H_ */
