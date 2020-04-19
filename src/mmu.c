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

#define ULL(x) x##ull

/* sctlr */

#define SCTLR_M_BIT		(ULL(1) << 0)
#define SCTLR_C_BIT		(ULL(1) << 2)

/* tcr */

#define TCR_RGN_INNER_NC        (ULL(0x0) << 8)
#define TCR_RGN_INNER_WBA       (ULL(0x1) << 8)
#define TCR_RGN_INNER_WT        (ULL(0x2) << 8)
#define TCR_RGN_INNER_WBNA      (ULL(0x3) << 8)

#define TCR_RGN_OUTER_NC        (ULL(0x0) << 10)
#define TCR_RGN_OUTER_WBA       (ULL(0x1) << 10)
#define TCR_RGN_OUTER_WT        (ULL(0x2) << 10)
#define TCR_RGN_OUTER_WBNA      (ULL(0x3) << 10)

#define TCR_SH_NON_SHAREABLE    (ULL(0x0) << 12)
#define TCR_SH_OUTER_SHAREABLE  (ULL(0x2) << 12)
#define TCR_SH_INNER_SHAREABLE  (ULL(0x3) << 12)

#define TCR_TG0_4K		(ULL(0) << 14)
#define TCR_TG0_64K		(ULL(1) << 14)
#define TCR_TG0_16K		(ULL(2) << 14)

#define TCR_EL3_PS_32BIT_4GB    (ULL(0) << 16)

#define TCR_EL3_RES1            ((ULL(1) << 31) | (ULL(1) << 23))

/* mair */

#define MAIR_DEV_nGnRnE         ULL(0x0)
#define MAIR_DEV_nGnRE          ULL(0x4)
#define MAIR_DEV_nGRE           ULL(0x8)
#define MAIR_DEV_GRE            ULL(0xc)
#define MAIR_NORM_WT_TR_WA      ULL(0x1)
#define MAIR_NORM_WT_TR_RA      ULL(0x2)
#define MAIR_NORM_WT_TR_RWA     ULL(0x3)
#define MAIR_NORM_NC            ULL(0x4)
#define MAIR_NORM_WB_TR_WA      ULL(0x5)
#define MAIR_NORM_WB_TR_RA      ULL(0x6)
#define MAIR_NORM_WB_TR_RWA     ULL(0x7)
#define MAIR_NORM_WT_NTR_NA     ULL(0x8)
#define MAIR_NORM_WT_NTR_WA     ULL(0x9)
#define MAIR_NORM_WT_NTR_RA     ULL(0xa)
#define MAIR_NORM_WT_NTR_RWA    ULL(0xb)
#define MAIR_NORM_WB_NTR_NA     ULL(0xc)
#define MAIR_NORM_WB_NTR_WA     ULL(0xd)
#define MAIR_NORM_WB_NTR_RA     ULL(0xe)
#define MAIR_NORM_WB_NTR_RWA    ULL(0xf)

#define MAIR_MEM(inner, outer)	((inner) | ((outer) << 4))
#define MAIR_ATTR(index, attr)	((attr) << ((index) * 8))

/* page table helpers */

#define BLOCK_DESC		ULL(0x1) /* Table levels 0-2 */
#define TABLE_DESC		ULL(0x3) /* Table levels 0-2 */

#define AP_RO				(ULL(1) << 5)
#define AP_RW				(ULL(0) << 5)
#define AP_ACCESS_UNPRIVILEGED		(ULL(1) << 4)
#define AP_NO_ACCESS_UNPRIVILEGED	(ULL(0) << 4)
#define AP_ONE_VA_RANGE_RES1		(ULL(1) << 4)
#define NS				(ULL(1) << 3)
#define NON_GLOBAL			(ULL(1) << 9)
#define ACCESS_FLAG			(ULL(1) << 8)
#define NSH				(ULL(0) << 6)
#define OSH				(ULL(2) << 6)
#define ISH				(ULL(3) << 6)
#define LOWER_ATTRS(x)			(((x) & ULL(0xfff)) << 2)

#define ATTR_NORMAL		ULL(0x0)
#define ATTR_DEV		ULL(0x1)
#define ATTR_NC			ULL(0x2)

#define PTE_DEV(pa) \
	(BLOCK_DESC | (pa) | \
	 LOWER_ATTRS(ACCESS_FLAG | OSH | AP_RW | AP_ONE_VA_RANGE_RES1 | \
		     ATTR_DEV))

#define PTE_MEM(pa) \
	(BLOCK_DESC | (pa) | \
	 LOWER_ATTRS(ACCESS_FLAG | ISH | AP_RW | AP_ONE_VA_RANGE_RES1 | \
		     ATTR_NORMAL))

#define PTE_TAB(pa) \
	(TABLE_DESC | (pa))

/*
 * This function creates an identity map via
 *
 * The size of the table is 8 KiB.
 */

void mmu_setup(uint64_t dram_size)
{
	uint64_t mair, tcr, ttbr, sctlr;

	/*
	 * Memory Attribute Indirection Register (EL3)
	 *
	 * https://developer.arm.com/docs/ddi0595/latest/aarch64-system-registers/mair_el3
	 *
	 * In page table, memory attributes are not set directly but read from
	 * mair_el3 register (at bit offset 8 * index).
	 */
	mair =  /* Device-nGnRE */
		MAIR_ATTR(ATTR_DEV, MAIR_DEV_nGnRE) |
		/* Normal Memory, Outer Write-Back non-transient, Inner Write-Back non-transient */
		MAIR_ATTR(ATTR_NORMAL, MAIR_MEM(MAIR_NORM_WB_NTR_RWA,
						MAIR_NORM_WB_NTR_RWA)) |
		/* Normal Memory, Outer Write-Through non-transient, Inner Non-cacheable */
		MAIR_ATTR(ATTR_NC, MAIR_MEM(MAIR_NORM_NC, MAIR_NORM_NC));

	/*
	 * Translation Control Register (EL3)
	 *
	 * https://developer.arm.com/docs/ddi0595/g/aarch64-system-registers/tcr_el3
	 *
	 * - page size is 4 KiB
	 * - physical address size is 4 GiB.
	 * - t0sz is the number of the most significant VA bits that must be
	 *   0 (so it defines virtual address range upper limit)
	 * - t0sz of 32 means that we'll start translation at L1 table
	 *
	 * Levels (for 4KiB granule):
	 * - L0 47:39  - skipped due to t0sz > 24
	 * - L1 38:30
	 * - L2 29:21
	 * - L3 20:12
	 */
	uint64_t t0sz = 32; /* 4 GiB */
	tcr = t0sz | TCR_TG0_4K | TCR_SH_INNER_SHAREABLE |
		TCR_RGN_OUTER_WBA | TCR_RGN_INNER_WBA |
		TCR_EL3_RES1 | TCR_EL3_PS_32BIT_4GB;

	/*
	 * Translation table base address.
	 *
	 * We place tt_base at the top of DRAM (8KiB from the end)
	 */
	uint64_t* tt_base = (uint64_t*)(uintptr_t)(0x40000000ull + dram_size - 4096 * 2);

	/*
	 * Generate the translation tables:
	 *
	 * L1 table with:
	 * - 1x  link to L2 table for the first 1GiB
	 * - 3x  1GiB blocks (cacheable for DRAM)
	 *
	 * L2 table for (0-1GiB) with:
	 * - 1x    2MiB blocks (cacheable for SRAM areas)
	 * - 511x  2MiB blocks (non-cacheable for device MMIO)
	 *
	 * Technically, each table has 512 8byte entries, but L1 table really
	 * only needs to fill in first 4 entries due to t0sz being 32.
	 */
	uint64_t* l1 = tt_base;
	uint64_t* l2 = tt_base + 512;

	*l1++ = PTE_TAB((uintptr_t)l2);
	for (int i = 1; i < 4; i++)
		*l1++ = PTE_MEM((1024ull * i) << 20);

	*l2++ = PTE_MEM((2ull * 0) << 20);
	for (int i = 1; i < 512; i++)
		*l2++ = PTE_DEV((2ull * i) << 20);

	/*
	 * Translation Table Base Register 0 (EL3)
	 *
	 * - A translation table must be aligned to the size of the table (4KiB
	 *   in our case)
	 */
	ttbr = (uintptr_t)tt_base;

	/* Invalidate all TLB entries */
	asm volatile("dsb ishst");
	asm volatile("tlbi alle3");

	/* Setup MMU registers */
	asm volatile("msr mair_el3, %0" : : "r" (mair) : "cc");
	asm volatile("msr tcr_el3,  %0" : : "r" (tcr) : "cc");
	asm volatile("msr ttbr0_el3, %0" : : "r" (ttbr) : "cc");

	asm volatile("dsb ish");
	asm volatile("isb");

	/* Enable MMU and data cache */
	asm volatile("mrs %0, sctlr_el3" : "=r" (sctlr) : : "cc");
	sctlr |= SCTLR_C_BIT | SCTLR_M_BIT;
	asm volatile("msr sctlr_el3, %0" : : "r" (sctlr) : "cc");

	asm volatile("isb");
}
