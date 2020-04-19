// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2007
 * Gerald Van Baren, Custom IDEAS, vanbaren@cideas.com
 *
 * Copyright 2010-2011 Freescale Semiconductor, Inc.
 */

#include <common.h>
//#include <env.h>
//#include <mapmem.h>
//#include <stdio_dev.h>
//#include <linux/ctype.h>
#include <linux/types.h>
//#include <asm/global_data.h>
#include <linux/libfdt.h>
#include <fdt_support.h>
//#include <exports.h>
//#include <fdtdec.h>

/**
 * fdt_getprop_u32_default_node - Return a node's property or a default
 *
 * @fdt: ptr to device tree
 * @off: offset of node
 * @cell: cell offset in property
 * @prop: property name
 * @dflt: default value if the property isn't found
 *
 * Convenience function to return a node's property or a default value if
 * the property doesn't exist.
 */
u32 fdt_getprop_u32_default_node(const void *fdt, int off, int cell,
				const char *prop, const u32 dflt)
{
	const fdt32_t *val;
	int len;

	val = fdt_getprop(fdt, off, prop, &len);

	/* Check if property exists */
	if (!val)
		return dflt;

	/* Check if property is long enough */
	if (len < ((cell + 1) * sizeof(uint32_t)))
		return dflt;

	return fdt32_to_cpu(*val);
}

/**
 * fdt_getprop_u32_default - Find a node and return it's property or a default
 *
 * @fdt: ptr to device tree
 * @path: path of node
 * @prop: property name
 * @dflt: default value if the property isn't found
 *
 * Convenience function to find a node and return it's property or a
 * default value if it doesn't exist.
 */
u32 fdt_getprop_u32_default(const void *fdt, const char *path,
				const char *prop, const u32 dflt)
{
	int off;

	off = fdt_path_offset(fdt, path);
	if (off < 0)
		return dflt;

	return fdt_getprop_u32_default_node(fdt, off, 0, prop, dflt);
}

/**
 * fdt_find_and_setprop: Find a node and set it's property
 *
 * @fdt: ptr to device tree
 * @node: path of node
 * @prop: property name
 * @val: ptr to new value
 * @len: length of new property value
 * @create: flag to create the property if it doesn't exist
 *
 * Convenience function to directly set a property given the path to the node.
 */
int fdt_find_and_setprop(void *fdt, const char *node, const char *prop,
			 const void *val, int len, int create)
{
	int nodeoff = fdt_path_offset(fdt, node);

	if (nodeoff < 0)
		return nodeoff;

	if ((!create) && (fdt_get_property(fdt, nodeoff, prop, NULL) == NULL))
		return 0; /* create flag not set; so exit quietly */

	return fdt_setprop(fdt, nodeoff, prop, val, len);
}

/**
 * fdt_find_or_add_subnode() - find or possibly add a subnode of a given node
 *
 * @fdt: pointer to the device tree blob
 * @parentoffset: structure block offset of a node
 * @name: name of the subnode to locate
 *
 * fdt_subnode_offset() finds a subnode of the node with a given name.
 * If the subnode does not exist, it will be created.
 */
int fdt_find_or_add_subnode(void *fdt, int parentoffset, const char *name)
{
	int offset;

	offset = fdt_subnode_offset(fdt, parentoffset, name);

	if (offset == -FDT_ERR_NOTFOUND)
		offset = fdt_add_subnode(fdt, parentoffset, name);

	if (offset < 0)
		printf("%s: %s: %s\n", __func__, name, fdt_strerror(offset));

	return offset;
}

static inline int fdt_setprop_uxx(void *fdt, int nodeoffset, const char *name,
				  uint64_t val, int is_u64)
{
	if (is_u64)
		return fdt_setprop_u64(fdt, nodeoffset, name, val);
	else
		return fdt_setprop_u32(fdt, nodeoffset, name, (uint32_t)val);
}

int fdt_initrd(void *fdt, ulong initrd_start, ulong initrd_end)
{
	int   nodeoffset;
	int   err, j, total;
	int is_u64;
	uint64_t addr, size;

	/* just return if the size of initrd is zero */
	if (initrd_start == initrd_end)
		return 0;

	/* find or create "/chosen" node. */
	nodeoffset = fdt_find_or_add_subnode(fdt, 0, "chosen");
	if (nodeoffset < 0)
		return nodeoffset;

	total = fdt_num_mem_rsv(fdt);

	/*
	 * Look for an existing entry and update it.  If we don't find
	 * the entry, we will j be the next available slot.
	 */
	for (j = 0; j < total; j++) {
		err = fdt_get_mem_rsv(fdt, j, &addr, &size);
		if (addr == initrd_start) {
			fdt_del_mem_rsv(fdt, j);
			break;
		}
	}

	err = fdt_add_mem_rsv(fdt, initrd_start, initrd_end - initrd_start);
	if (err < 0) {
		printf("fdt_initrd: %s\n", fdt_strerror(err));
		return err;
	}

	is_u64 = (fdt_address_cells(fdt, 0) == 2);

	err = fdt_setprop_uxx(fdt, nodeoffset, "linux,initrd-start",
			      (uint64_t)initrd_start, is_u64);

	if (err < 0) {
		printf("WARNING: could not set linux,initrd-start %s.\n",
		       fdt_strerror(err));
		return err;
	}

	err = fdt_setprop_uxx(fdt, nodeoffset, "linux,initrd-end",
			      (uint64_t)initrd_end, is_u64);

	if (err < 0) {
		printf("WARNING: could not set linux,initrd-end %s.\n",
		       fdt_strerror(err));

		return err;
	}

	return 0;
}

void do_fixup_by_prop(void *fdt,
		      const char *pname, const void *pval, int plen,
		      const char *prop, const void *val, int len,
		      int create)
{
	int off;
#if defined(DEBUG)
	int i;
	debug("Updating property '%s' = ", prop);
	for (i = 0; i < len; i++)
		debug(" %.2x", *(u8*)(val+i));
	debug("\n");
#endif
	off = fdt_node_offset_by_prop_value(fdt, -1, pname, pval, plen);
	while (off != -FDT_ERR_NOTFOUND) {
		if (create || (fdt_get_property(fdt, off, prop, NULL) != NULL))
			fdt_setprop(fdt, off, prop, val, len);
		off = fdt_node_offset_by_prop_value(fdt, off, pname, pval, plen);
	}
}

void do_fixup_by_prop_u32(void *fdt,
			  const char *pname, const void *pval, int plen,
			  const char *prop, u32 val, int create)
{
	fdt32_t tmp = cpu_to_fdt32(val);
	do_fixup_by_prop(fdt, pname, pval, plen, prop, &tmp, 4, create);
}

void do_fixup_by_compat(void *fdt, const char *compat,
			const char *prop, const void *val, int len, int create)
{
	int off = -1;
#if defined(DEBUG)
	int i;
	debug("Updating property '%s' = ", prop);
	for (i = 0; i < len; i++)
		debug(" %.2x", *(u8*)(val+i));
	debug("\n");
#endif
	off = fdt_node_offset_by_compatible(fdt, -1, compat);
	while (off != -FDT_ERR_NOTFOUND) {
		if (create || (fdt_get_property(fdt, off, prop, NULL) != NULL))
			fdt_setprop(fdt, off, prop, val, len);
		off = fdt_node_offset_by_compatible(fdt, off, compat);
	}
}

void do_fixup_by_compat_u32(void *fdt, const char *compat,
			    const char *prop, u32 val, int create)
{
	fdt32_t tmp = cpu_to_fdt32(val);
	do_fixup_by_compat(fdt, compat, prop, &tmp, 4, create);
}

#ifdef CONFIG_ARCH_FIXUP_FDT_MEMORY
/*
 * fdt_pack_reg - pack address and size array into the "reg"-suitable stream
 */
static int fdt_pack_reg(const void *fdt, void *buf, u64 *address, u64 *size,
			int n)
{
	int i;
	int address_cells = fdt_address_cells(fdt, 0);
	int size_cells = fdt_size_cells(fdt, 0);
	char *p = buf;

	for (i = 0; i < n; i++) {
		if (address_cells == 2)
			*(fdt64_t *)p = cpu_to_fdt64(address[i]);
		else
			*(fdt32_t *)p = cpu_to_fdt32(address[i]);
		p += 4 * address_cells;

		if (size_cells == 2)
			*(fdt64_t *)p = cpu_to_fdt64(size[i]);
		else
			*(fdt32_t *)p = cpu_to_fdt32(size[i]);
		p += 4 * size_cells;
	}

	return p - (char *)buf;
}

#if CONFIG_NR_DRAM_BANKS > 4
#define MEMORY_BANKS_MAX CONFIG_NR_DRAM_BANKS
#else
#define MEMORY_BANKS_MAX 4
#endif
int fdt_fixup_memory_banks(void *blob, u64 start[], u64 size[], int banks)
{
	int err, nodeoffset;
	int len, i;
	u8 tmp[MEMORY_BANKS_MAX * 16]; /* Up to 64-bit address + 64-bit size */

	if (banks > MEMORY_BANKS_MAX) {
		printf("%s: num banks %d exceeds hardcoded limit %d."
		       " Recompile with higher MEMORY_BANKS_MAX?\n",
		       __FUNCTION__, banks, MEMORY_BANKS_MAX);
		return -1;
	}

	err = fdt_check_header(blob);
	if (err < 0) {
		printf("%s: %s\n", __FUNCTION__, fdt_strerror(err));
		return err;
	}

	/* find or create "/memory" node. */
	nodeoffset = fdt_find_or_add_subnode(blob, 0, "memory");
	if (nodeoffset < 0)
			return nodeoffset;

	err = fdt_setprop(blob, nodeoffset, "device_type", "memory",
			sizeof("memory"));
	if (err < 0) {
		printf("WARNING: could not set %s %s.\n", "device_type",
				fdt_strerror(err));
		return err;
	}

	for (i = 0; i < banks; i++) {
		if (start[i] == 0 && size[i] == 0)
			break;
	}

	banks = i;

	if (!banks)
		return 0;

	len = fdt_pack_reg(blob, tmp, start, size, banks);

	err = fdt_setprop(blob, nodeoffset, "reg", tmp, len);
	if (err < 0) {
		printf("WARNING: could not set %s %s.\n",
				"reg", fdt_strerror(err));
		return err;
	}
	return 0;
}

int fdt_set_usable_memory(void *blob, u64 start[], u64 size[], int areas)
{
	int err, nodeoffset;
	int len;
	u8 tmp[8 * 16]; /* Up to 64-bit address + 64-bit size */

	if (areas > 8) {
		printf("%s: num areas %d exceeds hardcoded limit %d\n",
		       __func__, areas, 8);
		return -1;
	}

	err = fdt_check_header(blob);
	if (err < 0) {
		printf("%s: %s\n", __func__, fdt_strerror(err));
		return err;
	}

	/* find or create "/memory" node. */
	nodeoffset = fdt_find_or_add_subnode(blob, 0, "memory");
	if (nodeoffset < 0)
		return nodeoffset;

	len = fdt_pack_reg(blob, tmp, start, size, areas);

	err = fdt_setprop(blob, nodeoffset, "linux,usable-memory", tmp, len);
	if (err < 0) {
		printf("WARNING: could not set %s %s.\n",
		       "reg", fdt_strerror(err));
		return err;
	}

	return 0;
}
#endif

int fdt_fixup_memory(void *blob, u64 start, u64 size)
{
	return fdt_fixup_memory_banks(blob, &start, &size, 1);
}

int fdt_increase_size(void *fdt, int add_len)
{
	int newlen;

	newlen = fdt_totalsize(fdt) + add_len;

	/* Open in place with a new len */
	return fdt_open_into(fdt, fdt, newlen);
}

/* Resize the fdt to its actual size + a bit of padding */
int fdt_shrink_to_minimum(void *blob, uint extrasize)
{
        int i;
        uint64_t addr, size;
        int total, ret; 
        uint actualsize;
        int fdt_memrsv = 0; 

        if (!blob)
                return 0;

        total = fdt_num_mem_rsv(blob);
        for (i = 0; i < total; i++) {
                fdt_get_mem_rsv(blob, i, &addr, &size);
                if (addr == (uintptr_t)blob) {
                        fdt_del_mem_rsv(blob, i);
                        fdt_memrsv = 1; 
                        break;
                }    
        }    

        /*   
         * Calculate the actual size of the fdt
         * plus the size needed for 5 fdt_add_mem_rsv, one
         * for the fdt itself and 4 for a possible initrd
         * ((initrd-start + initrd-end) * 2 (name & value))
         */
        actualsize = fdt_off_dt_strings(blob) +
                fdt_size_dt_strings(blob) + 5 * sizeof(struct fdt_reserve_entry);

        actualsize += extrasize;
        /* Make it so the fdt ends on a page boundary */
        actualsize = ALIGN(actualsize + ((uintptr_t)blob & 0xfff), 0x1000);
        actualsize = actualsize - ((uintptr_t)blob & 0xfff);

        /* Change the fdt header to reflect the correct size */
        fdt_set_totalsize(blob, actualsize);

        if (fdt_memrsv) {
                /* Add the new reservation */
                ret = fdt_add_mem_rsv(blob, (uintptr_t)blob, actualsize);
                if (ret < 0) 
                        return ret; 
        }    

        return actualsize;
}

void fdt_del_node_and_alias(void *blob, const char *alias)
{
	int off = fdt_path_offset(blob, alias);

	if (off < 0)
		return;

	fdt_del_node(blob, off);

	off = fdt_path_offset(blob, "/aliases");
	fdt_delprop(blob, off, alias);
}

/* Max address size we deal with */
#define OF_MAX_ADDR_CELLS	4
#define OF_BAD_ADDR	FDT_ADDR_T_NONE
#define OF_CHECK_COUNTS(na, ns)	((na) > 0 && (na) <= OF_MAX_ADDR_CELLS && \
			(ns) > 0)

/**
 * fdt_alloc_phandle: Return next free phandle value
 *
 * @blob: ptr to device tree
 */
int fdt_alloc_phandle(void *blob)
{
	int offset;
	uint32_t phandle = 0;

	for (offset = fdt_next_node(blob, -1, NULL); offset >= 0;
	     offset = fdt_next_node(blob, offset, NULL)) {
		phandle = max(phandle, fdt_get_phandle(blob, offset));
	}

	return phandle + 1;
}

/*
 * fdt_set_phandle: Create a phandle property for the given node
 *
 * @fdt: ptr to device tree
 * @nodeoffset: node to update
 * @phandle: phandle value to set (must be unique)
 */
int fdt_set_phandle(void *fdt, int nodeoffset, uint32_t phandle)
{
	int ret;

#ifdef DEBUG
	int off = fdt_node_offset_by_phandle(fdt, phandle);

	if ((off >= 0) && (off != nodeoffset)) {
		char buf[64];

		fdt_get_path(fdt, nodeoffset, buf, sizeof(buf));
		printf("Trying to update node %s with phandle %u ",
		       buf, phandle);

		fdt_get_path(fdt, off, buf, sizeof(buf));
		printf("that already exists in node %s.\n", buf);
		return -FDT_ERR_BADPHANDLE;
	}
#endif

	ret = fdt_setprop_cell(fdt, nodeoffset, "phandle", phandle);
	if (ret < 0)
		return ret;

	/*
	 * For now, also set the deprecated "linux,phandle" property, so that we
	 * don't break older kernels.
	 */
	ret = fdt_setprop_cell(fdt, nodeoffset, "linux,phandle", phandle);

	return ret;
}

/*
 * fdt_create_phandle: Create a phandle property for the given node
 *
 * @fdt: ptr to device tree
 * @nodeoffset: node to update
 */
unsigned int fdt_create_phandle(void *fdt, int nodeoffset)
{
	/* see if there is a phandle already */
	int phandle = fdt_get_phandle(fdt, nodeoffset);

	/* if we got 0, means no phandle so create one */
	if (phandle == 0) {
		int ret;

		phandle = fdt_alloc_phandle(fdt);
		ret = fdt_set_phandle(fdt, nodeoffset, phandle);
		if (ret < 0) {
			printf("Can't set phandle %u: %s\n", phandle,
			       fdt_strerror(ret));
			return 0;
		}
	}

	return phandle;
}
