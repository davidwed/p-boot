/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2007
 * Gerald Van Baren, Custom IDEAS, vanbaren@cideas.com
 */

#ifndef __FDT_SUPPORT_H
#define __FDT_SUPPORT_H

#include <linux/libfdt.h>

u32 fdt_getprop_u32_default_node(const void *fdt, int off, int cell,
				const char *prop, const u32 dflt);
u32 fdt_getprop_u32_default(const void *fdt, const char *path,
				const char *prop, const u32 dflt);

/**
 * Add data to the root of the FDT before booting the OS.
 *
 * See doc/device-tree-bindings/root.txt
 *
 * @param fdt		FDT address in memory
 * @return 0 if ok, or -FDT_ERR_... on error
 */
int fdt_root(void *fdt);

/**
 * Add chosen data the FDT before booting the OS.
 *
 * In particular, this adds the kernel command line (bootargs) to the FDT.
 *
 * @param fdt		FDT address in memory
 * @return 0 if ok, or -FDT_ERR_... on error
 */
int fdt_chosen(void *fdt);

/**
 * Add initrd information to the FDT before booting the OS.
 *
 * @param fdt		FDT address in memory
 * @return 0 if ok, or -FDT_ERR_... on error
 */
int fdt_initrd(void *fdt, ulong initrd_start, ulong initrd_end);

void do_fixup_by_path(void *fdt, const char *path, const char *prop,
		      const void *val, int len, int create);
void do_fixup_by_path_u32(void *fdt, const char *path, const char *prop,
			  u32 val, int create);

static inline void do_fixup_by_path_string(void *fdt, const char *path,
					   const char *prop, const char *status)
{
	do_fixup_by_path(fdt, path, prop, status, strlen(status) + 1, 1);
}

void do_fixup_by_prop(void *fdt,
		      const char *pname, const void *pval, int plen,
		      const char *prop, const void *val, int len,
		      int create);
void do_fixup_by_prop_u32(void *fdt,
			  const char *pname, const void *pval, int plen,
			  const char *prop, u32 val, int create);
void do_fixup_by_compat(void *fdt, const char *compat,
			const char *prop, const void *val, int len, int create);
void do_fixup_by_compat_u32(void *fdt, const char *compat,
			    const char *prop, u32 val, int create);
/**
 * Setup the memory node in the DT. Creates one if none was existing before.
 * Calls fdt_fixup_memory_banks() to populate a single reg pair covering the
 * whole memory.
 *
 * @param blob		FDT blob to update
 * @param start		Begin of DRAM mapping in physical memory
 * @param size		Size of the single memory bank
 * @return 0 if ok, or -1 or -FDT_ERR_... on error
 */
int fdt_fixup_memory(void *blob, u64 start, u64 size);

/**
 * Fill the DT memory node with multiple memory banks.
 * Creates the node if none was existing before.
 * If banks is 0, it will not touch the existing reg property. This allows
 * boards to not mess with the existing DT setup, which may have been
 * filled in properly before.
 *
 * @param blob		FDT blob to update
 * @param start		Array of size <banks> to hold the start addresses.
 * @param size		Array of size <banks> to hold the size of each region.
 * @param banks		Number of memory banks to create. If 0, the reg
 *			property will be left untouched.
 * @return 0 if ok, or -1 or -FDT_ERR_... on error
 */
int fdt_fixup_memory_banks(void *blob, u64 start[], u64 size[], int banks);
int fdt_set_usable_memory(void *blob, u64 start[], u64 size[], int banks);

void set_working_fdt_addr(ulong addr);

/**
 * shrink down the given blob to minimum size + some extrasize if required
 *
 * @param blob		FDT blob to update
 * @param extrasize	additional bytes needed
 * @return 0 if ok, or -FDT_ERR_... on error
 */
int fdt_shrink_to_minimum(void *blob, uint extrasize);
int fdt_increase_size(void *fdt, int add_len);

int fdt_find_or_add_subnode(void *fdt, int parentoffset, const char *name);

int fdt_find_and_setprop(void *fdt, const char *node, const char *prop,
                         const void *val, int len, int create);

#endif
