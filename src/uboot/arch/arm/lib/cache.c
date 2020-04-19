// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2002
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

/* for now: just dummy functions to satisfy the linker */

#include <common.h>
#include <cpu_func.h>
#include <malloc.h>

/*
 * Flush range from all levels of d-cache/unified-cache.
 * Affects the range [start, start + size - 1].
 */
__weak void flush_cache(unsigned long start, unsigned long size)
{
	flush_dcache_range(start, start + size);
}

/*
 * Default implementation:
 * do a range flush for the entire range
 */
__weak void flush_dcache_all(void)
{
	flush_cache(0, ~0);
}

/*
 * Default implementation of enable_caches()
 * Real implementation should be in platform code
 */
__weak void enable_caches(void)
{
	puts("WARNING: Caches not enabled\n");
}

__weak void invalidate_dcache_range(unsigned long start, unsigned long stop)
{
	/* An empty stub, real implementation should be in platform code */
}
__weak void flush_dcache_range(unsigned long start, unsigned long stop)
{
	/* An empty stub, real implementation should be in platform code */
}

int check_cache_range(unsigned long start, unsigned long stop)
{
	int ok = 1;

	if (start & (CONFIG_SYS_CACHELINE_SIZE - 1))
		ok = 0;

	if (stop & (CONFIG_SYS_CACHELINE_SIZE - 1))
		ok = 0;

	if (!ok) {
		debug("CACHE: Misaligned operation at range [%08lx, %08lx]\n",
			     start, stop);
	}

	return ok;
}
