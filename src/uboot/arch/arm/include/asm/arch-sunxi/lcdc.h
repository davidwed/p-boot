/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Sunxi platform timing controller register and constant defines
 *
 * (C) Copyright 2014 Hans de Goede <hdegoede@redhat.com>
 * (C) Copyright 2017 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#ifndef _LCDC_H
#define _LCDC_H

struct sunxi_lcdc_reg {
	u32 ctrl;			/* 0x00 */
	u32 int0;			/* 0x04 */
	u32 int1;			/* 0x08 */
	u8 res0[0x04];			/* 0x0c */
	u32 tcon0_frm_ctrl;		/* 0x10 */
	u32 tcon0_frm_seed[6];		/* 0x14 */
	u32 tcon0_frm_table[4];		/* 0x2c */
	u8 res1[4];			/* 0x3c */
	u32 tcon0_ctrl;			/* 0x40 */
	u32 tcon0_dclk;			/* 0x44 */
	u32 tcon0_timing_active;	/* 0x48 */
	u32 tcon0_timing_h;		/* 0x4c */
	u32 tcon0_timing_v;		/* 0x50 */
	u32 tcon0_timing_sync;		/* 0x54 */
	u32 tcon0_hv_intf;		/* 0x58 */
	u8 res2[0x04];			/* 0x5c */
	u32 tcon0_cpu_intf;		/* 0x60 */
	u32 tcon0_cpu_wr_dat;		/* 0x64 */
	u32 tcon0_cpu_rd_dat0;		/* 0x68 */
	u32 tcon0_cpu_rd_dat1;		/* 0x6c */
	u32 tcon0_ttl_timing0;		/* 0x70 */
	u32 tcon0_ttl_timing1;		/* 0x74 */
	u32 tcon0_ttl_timing2;		/* 0x78 */
	u32 tcon0_ttl_timing3;		/* 0x7c */
	u32 tcon0_ttl_timing4;		/* 0x80 */
	u32 tcon0_lvds_intf;		/* 0x84 */
	u32 tcon0_io_polarity;		/* 0x88 */
	u32 tcon0_io_tristate;		/* 0x8c */
	u32 tcon1_ctrl;			/* 0x90 */
	u32 tcon1_timing_source;	/* 0x94 */
	u32 tcon1_timing_scale;		/* 0x98 */
	u32 tcon1_timing_out;		/* 0x9c */
	u32 tcon1_timing_h;		/* 0xa0 */
	u32 tcon1_timing_v;		/* 0xa4 */
	u32 tcon1_timing_sync;		/* 0xa8 */
	u8 res3[0x44];			/* 0xac */
	u32 tcon1_io_polarity;		/* 0xf0 */
	u32 tcon1_io_tristate;		/* 0xf4 */
	u32 ecc_fifo;			/* 0xf8 */
	u8 res4[0x104];			/* 0xfc */
	u32 mux_ctrl;			/* 0x200 */
	u8 res5[0x1c];			/* 0x204 */
	u32 lvds_ana0;			/* 0x220 */
	u32 lvds_ana1;			/* 0x224 */
};

/*
 * LCDC register constants.
 */
#define SUNXI_LCDC_X(x)				(((x) - 1) << 16)
#define SUNXI_LCDC_Y(y)				(((y) - 1) << 0)
#define SUNXI_LCDC_TCON_VSYNC_MASK		(1 << 24)
#define SUNXI_LCDC_TCON_HSYNC_MASK		(1 << 25)
#define SUNXI_LCDC_CTRL_IO_MAP_MASK		(1 << 0)
#define SUNXI_LCDC_CTRL_IO_MAP_TCON0		(0 << 0)
#define SUNXI_LCDC_CTRL_IO_MAP_TCON1		(1 << 0)
#define SUNXI_LCDC_CTRL_TCON_ENABLE		(1 << 31)
#define SUNXI_LCDC_TCON0_FRM_CTRL_RGB666	((1 << 31) | (0 << 4))
#define SUNXI_LCDC_TCON0_FRM_CTRL_RGB565	((1 << 31) | (5 << 4))
#define SUNXI_LCDC_TCON0_FRM_SEED		0x11111111
#define SUNXI_LCDC_TCON0_FRM_TAB0		0x01010000
#define SUNXI_LCDC_TCON0_FRM_TAB1		0x15151111
#define SUNXI_LCDC_TCON0_FRM_TAB2		0x57575555
#define SUNXI_LCDC_TCON0_FRM_TAB3		0x7f7f7777
#define SUNXI_LCDC_TCON0_CTRL_CLK_DELAY(n)	(((n) & 0x1f) << 4)
#define SUNXI_LCDC_TCON0_CTRL_ENABLE		(1 << 31)
#define SUNXI_LCDC_TCON0_CTRL_IF_8080		(1 << 24)
#define SUNXI_LCDC_TCON0_DCLK_DIV(n)		((n) << 0)
#define SUNXI_LCDC_TCON0_DCLK_ENABLE		(0xf << 28)
#define SUNXI_LCDC_TCON0_DCLK_ENABLE_1		(1 << 31)
#define SUNXI_LCDC_TCON0_TIMING_H_BP(n)		(((n) - 1) << 0)
#define SUNXI_LCDC_TCON0_TIMING_H_TOTAL(n)	(((n) - 1) << 16)
#define SUNXI_LCDC_TCON0_TIMING_V_BP(n)		(((n) - 1) << 0)
#define SUNXI_LCDC_TCON0_TIMING_V_TOTAL(n)	(((n) * 2) << 16)
#if defined(CONFIG_SUNXI_GEN_SUN6I) || defined(CONFIG_MACH_SUN8I_A83T)
#define SUNXI_LCDC_TCON0_LVDS_CLK_SEL_TCON0	(1 << 20)
#else
#define SUNXI_LCDC_TCON0_LVDS_CLK_SEL_TCON0	0 /* NA */
#endif
#define SUNXI_LCDC_TCON0_LVDS_IF_CLK_POL_NORMAL		(1 << 4)
#define SUNXI_LCDC_TCON0_LVDS_IF_DATA_POL_NORMAL	(0xf)
#define SUNXI_LCDC_TCON0_LVDS_INTF_BITWIDTH(n)	((n) << 26)
#define SUNXI_LCDC_TCON0_LVDS_INTF_ENABLE	(1 << 31)
#define SUNXI_LCDC_TCON0_IO_POL_DCLK_PHASE(x)	((x) << 28)
#define SUNXI_LCDC_TCON1_CTRL_CLK_DELAY(n)	(((n) & 0x1f) << 4)
#define SUNXI_LCDC_TCON1_CTRL_INTERLACE_ENABLE	(1 << 20)
#define SUNXI_LCDC_TCON1_CTRL_ENABLE		(1 << 31)
#define SUNXI_LCDC_TCON1_TIMING_H_BP(n)		(((n) - 1) << 0)
#define SUNXI_LCDC_TCON1_TIMING_H_TOTAL(n)	(((n) - 1) << 16)
#define SUNXI_LCDC_TCON1_TIMING_V_BP(n)		(((n) - 1) << 0)
#define SUNXI_LCDC_TCON1_TIMING_V_TOTAL(n)	((n) << 16)
#define SUNXI_LCDC_MUX_CTRL_SRC0_MASK		(0xf << 0)
#define SUNXI_LCDC_MUX_CTRL_SRC0(x)		((x) << 0)
#define SUNXI_LCDC_MUX_CTRL_SRC1_MASK		(0xf << 4)
#define SUNXI_LCDC_MUX_CTRL_SRC1(x)		((x) << 4)
#ifdef CONFIG_SUNXI_GEN_SUN6I
#define SUNXI_LCDC_LVDS_ANA0_EN_MB		(1 << 31)
#define SUNXI_LCDC_LVDS_ANA0_DRVC		(1 << 24)
#define SUNXI_LCDC_LVDS_ANA0_DRVD(x)		((x) << 20)
#define SUNXI_LCDC_LVDS_ANA0_EN_LDO		(1 << 30)
#define SUNXI_LCDC_LVDS_ANA0_EN_DRVC		(1 << 24)
#define SUNXI_LCDC_LVDS_ANA0_EN_DRVD(x)		(((x) & 0xf) << 20)
#define SUNXI_LCDC_LVDS_ANA0_C(x)		(((x) & 3) << 17)
#define SUNXI_LCDC_LVDS_ANA0_V(x)		(((x) & 3) << 8)
#define SUNXI_LCDC_LVDS_ANA0_PD(x)		(((x) & 3) << 4)
#else
#define SUNXI_LCDC_LVDS_ANA0			0x3f310000
#define SUNXI_LCDC_LVDS_ANA0_UPDATE		(1 << 22)
#endif
#define SUNXI_LCDC_LVDS_ANA1_INIT1		(0x1f << 26 | 0x1f << 10)
#define SUNXI_LCDC_LVDS_ANA1_INIT2		(0x1f << 16 | 0x1f << 00)
#define SUN4I_TCON_ECC_FIFO_EN			(1 << 3)

#define SUN4I_TCON0_CPU_IF_MODE_MASK			GENMASK(31, 28)
#define SUN4I_TCON0_CPU_IF_MODE_DSI			(1 << 28)
#define SUN4I_TCON0_CPU_IF_TRI_FIFO_FLUSH		BIT(16)
#define SUN4I_TCON0_CPU_IF_TRI_FIFO_EN			BIT(2)
#define SUN4I_TCON0_CPU_IF_TRI_EN			BIT(0)

#define SUN4I_TCON0_CPU_TRI0_REG		0x160
#define SUN4I_TCON0_CPU_TRI0_BLOCK_SPACE(space)		((((space) - 1) & 0xfff) << 16)
#define SUN4I_TCON0_CPU_TRI0_BLOCK_SIZE(size)		(((size) - 1) & 0xfff)

#define SUN4I_TCON0_CPU_TRI1_REG		0x164
#define SUN4I_TCON0_CPU_TRI1_BLOCK_NUM(num)		(((num) - 1) & 0xffff)

#define SUN4I_TCON0_CPU_TRI2_REG		0x168
#define SUN4I_TCON0_CPU_TRI2_START_DELAY(delay)		(((delay) & 0xffff) << 16)
#define SUN4I_TCON0_CPU_TRI2_TRANS_START_SET(set)	((set) & 0xfff)

#define SUN4I_TCON_SAFE_PERIOD_REG		0x1f0
#define SUN4I_TCON_SAFE_PERIOD_NUM(num)			(((num) & 0xfff) << 16)
#define SUN4I_TCON_SAFE_PERIOD_MODE(mode)		((mode) & 0x3)

#endif /* _LCDC_H */
