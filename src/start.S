/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2013
 * David Feng <fenghua@phytium.com.cn>
 *
 * This code was taken from U-Boot and adapted by:
 * Copyright (C) 2020  Ondřej Jirman <megi@xff.cz>
 */

#define COUNTER_FREQUENCY 24000000

/* Use end of SRAM A2 for now, as SRAM A1 is pretty tight for an ARM64 build. */
#define SRAM_STACK_A2     0x00054000
#define DRAM_MAIN         0x80000000

	.global _start

_start:
	/*
	 * Switch into AArch64 if needed.
	 *
	 * TODO: we can try to run in aarch32/thumb mode up until jumping to ATF
	 * and this could save some space for more features.
	 */
        tst     x0, x0                  // this is "b #0x84" in ARM
        b       _reset
        .space  0x7c
        .word   0xe59f1024      // ldr     r1, [pc, #36] ; RVBAR_ADDR below
        .word   0xe59f0024      // ldr     r0, [pc, #36] ; _start symbol address below
        .word   0xe5810000      // str     r0, [r1]
        .word   0xf57ff04f      // dsb     sy
        .word   0xf57ff06f      // isb     sy
        .word   0xee1c0f50      // mrc     15, 0, r0, cr12, cr0, {2} ; RMR
        .word   0xe3800003      // orr     r0, r0, #3
        .word   0xee0c0f50      // mcr     15, 0, r0, cr12, cr0, {2} ; RMR
        .word   0xf57ff06f      // isb     sy
        .word   0xe320f003      // wfi
        .word   0xeafffffd      // b       @wfi
        .word   0x017000a0      // writeable RVBAR mapping address
        .word   _start

	.global _reset

_reset:
	mrs	x0, scr_el3
	orr	x0, x0, #0xf			/* SCR_EL3.NS|IRQ|FIQ|EA */
	msr	scr_el3, x0
	msr	cptr_el3, xzr			/* Enable FP/SIMD */
	ldr	x0, =COUNTER_FREQUENCY
	msr	cntfrq_el0, x0			/* Initialize CNTFRQ */

	/* BROM/FEL loaded our image just now, so we don't need to init BSS. */
        ldr     w0, =SRAM_STACK_A2
        bic     sp, x0, #0xf
#ifdef DRAM_STACK_SWITCH
        bl      main_sram_only

	/* Run main() with stack in DRAM, since we'll be loading SCP to SRAM A2 */
        ldr     w0, _dram_stack_top
        bic     sp, x0, #0xf
#endif
        bl      main

#ifdef RETURN_TO_DRAM_MAIN
	/* Normally main() will return only if it wants to jump to a bootloader
	 * part that is loaded in DRAM */
	ldr	w0, globals
	ldr	w1, =DRAM_MAIN
        blr     x1
#endif

0:
	wfi
	b 0b

#ifdef DRAM_STACK_SWITCH
	.global _dram_stack_top
_dram_stack_top:
	.word 0
#endif
