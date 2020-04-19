/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2009
 * Marvell Semiconductor <www.marvell.com>
 * Written-by: Prafulla Wadaskar <prafulla@marvell.com>
 */

#ifndef _ASM_CACHE_H
#define _ASM_CACHE_H

#include <asm/system.h>

/*
 * The value of the largest data cache relevant to DMA operations shall be set
 * for us in CONFIG_SYS_CACHELINE_SIZE.  In some cases this may be a larger
 * value than found in the L1 cache but this is OK to use in terms of
 * alignment.
 */
#define ARCH_DMA_MINALIGN	CONFIG_SYS_CACHELINE_SIZE

#endif /* _ASM_CACHE_H */
