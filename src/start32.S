/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2013
 * David Feng <fenghua@phytium.com.cn>
 *
 * This code was taken from U-Boot and adapted by:
 * Copyright (C) 2020  Ondřej Jirman <megi@xff.cz>
 *
 * This is aarch32 code that basically acts as an extenstion of BROM. We have stack
 * set up from BROM upon entry.
 *
 * We can jump to:
 * - 0x20 to enter FEL
 * - 0x2f54 to make BROM boot from eMMC
 */

/*
 * Entry point is at fixed offset 0x100e4 and it's entered in A32 state.
 */
        .section .entry,"awx"
	.global entry
entry:
	blx main
	blx exit

        .section .text
	.thumb
	.syntax unified
	.global exit
exit:
        ldr     r1, =0x017000a0 // RVBAR_ADDR
        ldr     r0, =0x10060    // aarch64 bootloader code _start
        str     r0, [r1]
        dsb     sy
        isb     sy
        mrc     15, 0, r0, cr12, cr0, 2 // RMR
        orr     r0, r0, #3
        mcr     15, 0, r0, cr12, cr0, 2 // RMR
        isb     sy
0:
        wfi
        b       0b

	.global jump_to
jump_to:
	bx	r0
