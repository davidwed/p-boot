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
	 * The following instruction is "b #0x84" in aarch32, and jumps into
	 * start-aa32.bin.
	 */
        tst     x0, x0
        b       _reset
	.incbin "p-boot-start32.bin"

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
