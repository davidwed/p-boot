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
#include <asm/arch/gpio.h>
#include <asm/arch/clock.h>
#include <asm-generic/gpio.h>
#include <asm/arch/dram.h>
#include <asm/armv8/mmu.h>
#include <linux/compiler.h>
#include <linux/libfdt.h>
#include <fdt_support.h>
#include <cpu_func.h>
#include <asm/system.h>
#include <asm/arch/clock.h>
#include <asm/arch/spl.h>
#include <asm/arch/prcm.h>
#include <atf_common.h>
#include "debug.h"
#include "mmu.h"
#include "pmic.h"
#include "ccu.h"
#include "bootfs.h"
#include "lradc.h"
#include "display.h"
#include "vidconsole.h"
#include "gui.h"
#include "storage.h"
#include "strbuf.h"
#include <build-ver.h>

#define DRAM_MAIN         0x80000000

// DRAM:

// {{{ Globals

struct globals {
	uint32_t dram_size;
	uint32_t boot_source;

	// storage for stdout
	char* log_start;
	char* log_end;

	uintptr_t linux_image_pa;

	uint8_t* heap_end;
	ulong t0;

	uint32_t sid[4];
	int board_rev;

	bool uvlo_shutdown;
	bool vbus_powerup;
	bool pok_powerup;
	bool vbus_preset;
	bool bat_present;
	bool bat_safe_mode;
	bool bat_charging;
	int bat_capacity;

	struct bootfs* sd; // NULL if not loaded or failed
	struct bootfs* emmc;
	uint32_t mmc_tried; // bitmask of mmcs that were already loaded
};

struct globals* globals = NULL;

// }}}
// {{{ Simple heap implementation


void* malloc(size_t len)
{
	void* p = globals->heap_end;
	globals->heap_end += ALIGN(len, 64);
	return p;
}

void* zalloc(size_t len)
{
	void* p = malloc(len);
	memset(p, 0, len);
	return p;
}

void free(void* p)
{
}

// }}}
// {{{ DRAM

#ifdef PBOOT_FDT_LOG

#define TMP_LOG_START (char*)(uintptr_t)0x00018000

// place log in SRAM C until DRAM is ready
static char* tmp_log_end = TMP_LOG_START;

void append_log(char c)
{
	if (c != '\r') {
		if (globals)
			*globals->log_end++ = c;
		else
			*tmp_log_end++ = c;
	}
}

#endif

// }}}
// {{{ Storage

static void mmc_try_load(uint32_t mask)
{
	if (mask & BIT(0) & ~globals->mmc_tried)
		globals->sd = bootfs_open(mmc_probe(0));
	if (mask & BIT(2) & ~globals->mmc_tried)
		globals->emmc = bootfs_open(mmc_probe(2));

	globals->mmc_tried |= mask;
}

// }}}
// {{{ Boot configuration selection

/* 0-31 = eMMC, 32-63 = SD, 64 = POWEROFF, 65 = reboot to EMMC, 66 = reboot to FEL */
enum {
	BOOTSEL_POWEROFF = 64,
	BOOTSEL_EMMC,
	BOOTSEL_FEL,
};

struct bootsel {
	int def; // -1 = undefined
	int next; // -1 = undefined
};

static void rtc_bootsel_get(struct bootsel* sel)
{
        uint32_t v = readl((ulong)SUNXI_RTC_BASE + 0x100);

	sel->next = (int)((v >> 8) & 0xff) - 1;
	sel->def = (int)(v & 0xff) - 1;
}

static void rtc_bootsel_set(struct bootsel* sel)
{
	uint32_t v = 0;

	v |= ((sel->next + 1) & 0xff) << 8;
	v |= ((sel->def + 1) & 0xff);

	writel(v, (ulong)SUNXI_RTC_BASE + 0x100);
}

// determine what bootfs option and filesystem to use based on RTC register
static struct bootfs_conf* bootfs_select_configuration(int* opt,
						       struct bootfs** fs_out)
{
	struct bootfs* fs = NULL;
	struct bootfs_conf* bc;
	struct bootfs_conf* sel = NULL;
	struct bootsel rtcsel;
	int bootsel;

	/* override boot configuration based on RTC data register */
	rtc_bootsel_get(&rtcsel);
	if (rtcsel.next >= 0) {
		rtcsel.next = -1;
		bootsel = rtcsel.next;
		rtc_bootsel_set(&rtcsel);
	} else if (rtcsel.def >= 0) {
		bootsel = rtcsel.def;
	} else {
		*opt = -1;
		goto out;
	}

        *opt = bootsel;

	if (bootsel < 32) {
		mmc_try_load(BIT(2));
		fs = globals->emmc;
	} else if (bootsel < 64) {
		mmc_try_load(BIT(0));
		fs = globals->sd;
		bootsel -= 32;
	} else {
		goto out;
	}

	if (!fs)
		goto out;

	bc = fs->confs_blocks;

	if (memcmp(bc[bootsel].magic, ":BFCONF:", 8))
		goto out;

	sel = &bc[bootsel];

out:
	*fs_out = fs;
	return sel;
}

// }}}

// SoC/board features:

// {{{ LEDs control

static void green_led_set(bool on)
{
	gpio_direction_output(SUNXI_GPD(18), on); // green pinephone led
}

static void red_led_set(bool on)
{
        gpio_direction_output(SUNXI_GPD(19), on); // red on
}

// }}}
// {{{ SoC clocks initialization

static void clocks_init(void)
{
	struct sunxi_ccm_reg * const ccm =
		(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;

	clock_set_pll1(816000000);

	writel(0x00003180, &ccm->ahb1_apb1_div);
	writel(0x1, &ccm->axi_gate);
}

// }}}
// {{{ SoC ID

static void get_soc_id(uint32_t sid[4])
{
	int i;
	for (i = 0; i < 4; i++)
		sid[i] = readl((ulong)SUNXI_SID_BASE + 4 * i);
}

static bool get_mac_address(unsigned no, uint8_t mac_addr[6])
{
	uint32_t* sid = globals->sid;
	if (sid[0] == 0)
		return false;

	sid[3] = sid[1] ^ sid[2] ^ sid[3];
        // U-Boot uses: sid[3] = crc32(0, (unsigned char *)&sid[1], 12);
	// but we don't have CRC routine

	if ((sid[3] & 0xffffff) == 0)
		sid[3] |= 0x800000;

	mac_addr[0] = (no << 4) | 0x02;
	mac_addr[1] = (sid[0] >>  0) & 0xff;
	mac_addr[2] = (sid[3] >> 24) & 0xff;
	mac_addr[3] = (sid[3] >> 16) & 0xff;
	mac_addr[4] = (sid[3] >>  8) & 0xff;
	mac_addr[5] = (sid[3] >>  0) & 0xff;

	return true;
}

#define SUNXI_BOOTED_FROM_FEL 0xff

static uint32_t get_boot_source(void)
{
        if (!is_boot0_magic(SPL_ADDR + 4))
                return SUNXI_BOOTED_FROM_FEL;

        return readb((ulong)SPL_ADDR + 0x28);
}

// }}}
// {{{ RTC

static void rtc_fixup(void)
{
        // check if RTC runs at lower value than build date, move to build date
        uint32_t ymd = readl((ulong)SUNXI_RTC_BASE + 0x10);
        uint32_t hms = readl((ulong)SUNXI_RTC_BASE + 0x14);
        uint32_t rtc_year = 1970 + ((ymd >> 16) & 0x3f);
        if (rtc_year < BUILD_YEAR) {
                printf("RTC: %08x %08x (<BUILD_DATE)\n", ymd, hms);

                ymd = ((BUILD_YEAR - 1970) & 0x3f) << 16;
                ymd |= (BUILD_MONTH << 8) | BUILD_DAY;

                writel(ymd, (ulong)SUNXI_RTC_BASE + 0x10);
                writel(0, (ulong)SUNXI_RTC_BASE + 0x14);
        }
}

// }}}
// {{{ Watchdog

static void soc_reset(void)
{
        /* Set the watchdog for its shortest interval (.5s) and wait */
        writel(1, (ulong)(SUNXI_TIMER_BASE + 0xB4));
        writel(1, (ulong)(SUNXI_TIMER_BASE + 0xB8));
        writel((0xa57 << 1) | 1, (ulong)(SUNXI_TIMER_BASE + 0xB0));

        udelay(1000000);
        panic(4, "SoC reset failed");
}

static void wdog_ping(void)
{
	// reset system
	writel(1, (ulong)(SUNXI_TIMER_BASE + 0xB4));

	// enable and set to 10s
	writel(0x81, (ulong)(SUNXI_TIMER_BASE + 0xB8));

	// reset watchdog
	writel((0xa57 << 1) | 1, (ulong)(SUNXI_TIMER_BASE + 0xB0));
}

static void wdog_disable(void)
{
	// enable and set to 10s
	writel(0, (ulong)(SUNXI_TIMER_BASE + 0xB8));
}

// }}}
// {{{ PMIC initialization

// 3kOhm NTC inside PinePhone batery
// ---------------------------------
//
// Charging:
//  0 - 15 °C: Max 0.2C CC to 4.35V : 9750 Ohm - 4710 Ohm
// 15 - 50 °C: Max 0.5C CC to 4.35V : 4710 Ohm - 1080 Ohm
//
// Discharging:
// -10 °C : 16500 Ohm
//  55 °C : 896 Ohm

static void pmic_setup_bat_temp_sensor(void)
{
	// enable TS pin input to ADC
	pmic_clrsetbits(0x82, 0, BIT(0));

	// safety thresholds:

	// voltage = reg_val * 12800 uV (range is 0 - 3.264V)
	pmic_write(0x38,  9750 * 80 / 12800); // V_LTF-charge
	pmic_write(0x39,  1080 * 80 / 12800); // V_HTF-charge
	pmic_write(0x3c, 16500 * 80 / 12800); // V_LTF-work
	pmic_write(0x3d,   896 * 80 / 12800); // V_HTF-work

	// There is a hysteresis of 460.8 mV(refer to TS pin voltage) for UTP
	// threshold, and there is a hysteresis of 57.6 mV for OTP threshold.

	// use TS pin only when charging, make it affect the charger, I = 80uA
	pmic_clrsetbits(0x84, 0x37, 0x31);
}

/* PinePhone battery data, TODO: read from FDT */
static const uint8_t ocv_tab[] = {
	0,  1,   1,  2 , 2,  4,  4,  5,
	6,  8,  11, 15, 22, 36, 44, 48,
	51, 53, 56, 60, 64, 69, 71, 75,
	78, 82, 84, 85, 88, 92, 95, 98,
};

static void pmic_setup_ocv_table(void)
{
	// OCV table 0xc0 - 0xdf
        for (int i = 0; i < ARRAY_SIZE(ocv_tab); i++)
                pmic_write(0xc0 + i, ocv_tab[i]);
}

static void pmic_read_status(void)
{
        int status0, status1, status2, capacity;

	// read status registers
	status0 = pmic_read(0x00);
	status1 = pmic_read(0x01);
	status2 = pmic_read(0x02);

	// clear power up status
	pmic_write(0x02, 0xff);

	// setup PMIC status in globals
	if (status0 >= 0 && status1 >= 0 && status2 >= 0) {
		globals->uvlo_shutdown = status2 & BIT(5);
		globals->vbus_powerup = status2 & BIT(1);
		globals->pok_powerup = status2 & BIT(0);
		globals->vbus_preset = status0 & BIT(5);
		if (status1 & BIT(5) && status1 & BIT(4)) {
			globals->bat_present = true;
			globals->bat_charging = status0 & BIT(2);
			if (status1 & BIT(3))
				globals->bat_safe_mode = true;
		}
	}

	// battery capacity
	globals->bat_capacity = -1;
	if (globals->bat_present) {
		int bat_capacity = pmic_read(0xb9);
		if (bat_capacity >= 0 && bat_capacity & 0x80)
			globals->bat_capacity = bat_capacity & 0x7f;
	}
}

// }}}
// {{{ Detect PinePhone hardware

static int detect_pinephone_revision(void)
{
	int ret = 1;

	sunxi_gpio_set_pull(SUNXI_GPL(6), SUNXI_GPIO_PULL_UP);
	sunxi_gpio_set_cfgpin(SUNXI_GPL(6), SUNXI_GPIO_INPUT);

	udelay(100);

	/* PL6 is pulled low by the modem on v1.2. */
	if (!gpio_get_value(SUNXI_GPL(6)))
		ret = 2;

	sunxi_gpio_set_cfgpin(SUNXI_GPL(6), SUNXI_GPIO_DISABLE);
	sunxi_gpio_set_pull(SUNXI_GPL(6), SUNXI_GPIO_PULL_DISABLE);

	return ret;
}

// }}}

// Booting:

// {{{ FDT

static const char* fdt_get_alias_node(void* fdt, const char* alias)
{
	const struct fdt_property *fdt_prop;
	const char* path = fdt_get_alias(fdt, alias);
	if (!path)
		return NULL;

	int node = fdt_path_offset(fdt, path);
	if (node < 0)
		return NULL;

	fdt_prop = fdt_get_property(fdt, node, "status", NULL);
	if (fdt_prop && !strcmp(fdt_prop->data, "disabled"))
		return NULL;

	return path;
}

static void print_mac(uint8_t mac[6])
{
	for (int i = 0; i < 6; i++) {
		if (i > 0)
			putc(':');
		put_hex(mac[i], 2, 1);
	}
}

static void fdt_fixup_wifi(void* fdt)
{
	const char* path;
	uint8_t mac_addr[6];
	int ret;

	path = fdt_get_alias_node(fdt, "ethernet0");
	if (!path) {
		printf("WiFi node not found\n");
		return;
	}

	if (!get_mac_address(0, mac_addr)) {
		printf("MAC address generator failed\n");
		return;
	}

	puts("WiFi MAC: ");
	print_mac(mac_addr);
	puts("\n");

        ret = fdt_find_and_setprop(fdt, path, "mac-address", mac_addr, 6, 0);
        if (ret)
                printf("WiFi MAC addr update failed err=%d\n", ret);

        ret = fdt_find_and_setprop(fdt, path, "local-mac-address", mac_addr, 6, 1);
        if (ret)
                printf("WiFi local MAC addr update failed err=%d\n", ret);
}

static void fdt_fixup_bt(void* fdt)
{
	const char* path;
	uint8_t mac_addr[6];
	int ret;

	path = fdt_get_alias_node(fdt, "bluetooth0");
	if (!path) {
		printf("BT node not found\n");
		return;
	}

	if (!get_mac_address(1, mac_addr)) {
		printf("MAC address generator failed\n");
		return;
	}

	puts("BT MAC: ");
	print_mac(mac_addr);
	puts("\n");

	// reverse byte order
	for (int i = 0; i < 3; i++) {
		uint8_t tmp = mac_addr[i];
		mac_addr[i] = mac_addr[5 - i];
		mac_addr[5 - i] = tmp;
	}

        ret = fdt_find_and_setprop(fdt, path, "local-bd-address", mac_addr, 6, 1);
        if (ret)
                printf("BT local MAC addr update failed err=%d\n", ret);
}

static void fdt_add_pboot_data(void* fdt_blob, struct bootfs* fs)
{
	struct bootfs_conf* bc = fs->confs_blocks;
	char* configs = malloc(32 * 256);
	char* p = configs;

	for (int i = 0; i < 32; i++) {
		struct bootfs_conf* c = &bc[i];

		if (!memcmp(c->magic, ":BFCONF:", 8)) {
			int dig1 = i / 10;
			int dig2 = i % 10;

			// write out boot conf index
			if (dig1)
				*p++ = '0' + dig1;
			*p++ = '0' + dig2;

			// separator
			*p++ = ':';

			// boot config name
			for (int j = 0; j < sizeof(c->name) && c->name[j]; j++)
				*p++ = c->name[j];
			*p++ = '\n';
		}
	}

        int pboot_off = fdt_find_or_add_subnode(fdt_blob, 0, "p-boot");
        if (pboot_off < 0)
		return;

	fdt_setprop(fdt_blob, pboot_off, "configs",
		    configs, p - configs);

#ifdef PBOOT_FDT_LOG
	fdt_setprop(fdt_blob, pboot_off, "log",
		    globals->log_start, globals->log_end - globals->log_start);
#endif
}

// }}}
// {{{ ATF entry/exit helpers

// }}}
// {{{ Linux boot

/*
 * SRAM/DRAM layout during boot:
 *
 * 0x00044000 - ATF
 * 0x40080000 - Linux Image
 * 0x4a000000 - DTB
 * 0x4b000000 - DTB2 (alternative dtb)
 * 0x4fe00000 - initramfs
 *
 * ATF maps 0x4a000000 - 0x4c000000 for main u-boot binary. We don't have u-boot
 * binary, but we want to place FDT there so that ATF can read it, without
 * setting up any new mappings. ATF needs to be patched to find the FDT there.
 */

#define ATF_PA		0x44000
#define LINUX_IMAGE_PA	0x40000000
#define FDT_BLOB_PA	0x4a000000
#define FDT_BLOB2_PA	0x4b000000
#define INITRAMFS_PA	0x4fe00000

enum {
	IMAGE_ATF,
	IMAGE_FDT,
	IMAGE_LINUX,
	// put required images above this line
	IMAGE_INITRD,
	IMAGE_FDT2,
	IMAGE_COUNT,
};

struct boot
{
	uint32_t loaded_images;
	uint64_t image_offsets[IMAGE_COUNT];
	uint32_t image_sizes[IMAGE_COUNT];
	uint32_t image_dests[IMAGE_COUNT];
	void* fdt;
	char bootargs[4096];
	struct bootfs* fs;
	struct bootfs_conf* conf;
};

struct kernel_image_hdr {
	u32 code0;                    /* Executable code */
	u32 code1;                    /* Executable code */
	u64 text_offset;              /* Image load offset, little endian */
	u64 image_size;               /* Effective Image size, little endian */
	u64 flags;                    /* kernel flags, little endian */
	u64 res2;		      /* reserved */
	u64 res3;		      /* reserved */
	u64 res4;                     /* reserved */
	u32 magic;		      /* Magic number, little endian, "ARM\x64" */
	u32 res5;                     /* reserved (used for PE COFF offset) */
};

static const char* img_names[] = {
	[IMAGE_ATF] = "ATF(+SCP)",
	[IMAGE_FDT] = "FDT",
	[IMAGE_FDT2] = "FDT2",
	[IMAGE_LINUX] = "Linux",
	[IMAGE_INITRD] = "Initrd",
};

bool boot_prepare(struct boot* boot, struct bootfs* fs, struct bootfs_conf* bc)
{
	// read the images from the selected table entry to memory
        memset(boot, 0, sizeof *boot);

	boot->fs = fs;
	boot->conf = bc;

	for (int j = 0; j < ARRAY_SIZE(bc->images); j++) {
		struct bootfs_image* im = &bc->images[j];
		unsigned type = __be32_to_cpu(im->type);
		uintptr_t dest = 0;
		int image_kind = -1;

		switch (type) {
			case 'A':
				dest = ATF_PA;
				image_kind = IMAGE_ATF;
				break;
			case 'D':
				dest = FDT_BLOB_PA;
				image_kind = IMAGE_FDT;
				break;
			case '2':
				dest = FDT_BLOB2_PA;
				image_kind = IMAGE_FDT2;
				break;
			case 'L':
				dest = LINUX_IMAGE_PA;
				image_kind = IMAGE_LINUX;
				break;
			case 'I':
				dest = INITRAMFS_PA;
				image_kind = IMAGE_INITRD;
				break;
		}

		if (image_kind < 0)
			continue;

		boot->image_dests[image_kind] = dest;
		boot->image_offsets[image_kind] = __be32_to_cpu(im->data_off);
		boot->image_sizes[image_kind] = __be32_to_cpu(im->data_len);
		boot->loaded_images |= 1 << image_kind;
	}

	bool missing = false;
	for (int i = 0; i < IMAGE_INITRD; i++) {
		if (!(boot->loaded_images & (1 << i))) {
			printf("Missing %s image\n", img_names[i]);
			missing = true;
		}
	}

	if (missing)
		return false;

	for (int i = 0; i < IMAGE_COUNT; i++) {
		if (!(boot->loaded_images & (1 << i)))
			continue;

		ssize_t size = bootfs_load_image(fs, boot->image_dests[i],
						 boot->image_offsets[i],
						 boot->image_sizes[i],
						 img_names[i]);
		if (size < 0)
			return false;
	}

	// if alternate FDT is present, assume it's for 1.2 and if 1.2 is detected
	// use it
	if (boot->loaded_images & (1 << IMAGE_FDT2)) {
		if (globals->board_rev == 2) {
			memcpy((void*)(uintptr_t)boot->image_dests[IMAGE_FDT],
			       (void*)(uintptr_t)boot->image_dests[IMAGE_FDT2],
			       boot->image_sizes[IMAGE_FDT2]);
		}
	}

	struct kernel_image_hdr* h = (void*)(uintptr_t)boot->image_dests[IMAGE_LINUX];
	if (h->magic != 0x644d5241) {
                printf("Linux image is not an arm64 kernel image\n");
		return false;
	}

	globals->linux_image_pa = boot->image_dests[IMAGE_LINUX];
	if (h->text_offset != 0) {
		// relocate kernel with a non-0 text_offset
		globals->linux_image_pa += h->text_offset;
		memmove((void*)(uintptr_t)globals->linux_image_pa,
			(void*)(uintptr_t)boot->image_dests[IMAGE_LINUX],
			boot->image_sizes[IMAGE_LINUX]);
	}

	boot->fdt = (void*)(uintptr_t)boot->image_dests[IMAGE_FDT];

        int err = fdt_check_header(boot->fdt);
        if (err < 0) {
                printf("Bad FDT hedaer: %s\n", fdt_strerror(err));
		return false;
	}

	err = fdt_increase_size(boot->fdt, 1024 * 256); // generous
        if (err < 0) {
                printf("Can't expand FDT: %s\n", fdt_strerror(err));
		return false;
	}

	memcpy(boot->bootargs, bc->boot_args, strlen((char*)bc->boot_args) + 1);

        int chosen_off = fdt_find_or_add_subnode(boot->fdt, 0, "chosen");
        if (chosen_off < 0) {
                printf("Can't create /chosen node\n");
		return false;
	}

	return true;
}

void boot_append_bootargs(struct boot* boot, char* bootargs)
{
	int len = strlen(boot->bootargs);

	boot->bootargs[len] = ' ';
	memcpy(boot->bootargs + len + 1, bootargs, strlen(bootargs) + 1);
}

int fdt_setup_framebuffer(struct boot* boot, uint32_t fb_addr)
{
	int ret;
	void* fdt = boot->fdt;
	uint32_t size = 1440 * 720 * 4;
	fdt32_t cells[] = {
		cpu_to_fdt32(fb_addr),
		cpu_to_fdt32(size),
	};

        int chosen_off = fdt_find_or_add_subnode(boot->fdt, 0, "chosen");
        if (chosen_off < 0) {
                printf("Can't create /chosen node\n");
		return false;
	}

        ret = fdt_setprop(fdt, chosen_off, "p-boot,framebuffer", cells, sizeof cells);
        if (ret)
		goto err;

        ret = fdt_setprop_u32(fdt, chosen_off, "p-boot,framebuffer-start", fb_addr);
        if (ret)
		goto err;

	ret = fdt_add_mem_rsv(fdt, fb_addr, size);
        if (ret)
		goto err;

	return 0;

err:
	printf("simplefb setup failed err=%d\n", ret);
	return -1;
}

bool boot_finalize(struct boot* boot)
{
	int err;
	void* fdt = boot->fdt;

        // setup FDT
	fdt_fixup_wifi(fdt);

	// PinePhone doesn't need BT local address fixup
	//fdt_fixup_bt(fdt);
	fdt_add_pboot_data(fdt, boot->fs);

	//printf("args: %s\n", boot->bootargs);

        int chosen_off = fdt_find_or_add_subnode(boot->fdt, 0, "chosen");
        if (chosen_off < 0) {
                printf("Can't create /chosen node\n");
		return false;
	}

	err = fdt_setprop(fdt, chosen_off, "bootargs",
			  boot->bootargs, strlen(boot->bootargs) + 1);
	if (err < 0) {
		printf("Can't set bootargs %d (%s)\n", err, boot->bootargs);
		return false;
	}

	err = fdt_fixup_memory(fdt, 0x40000000, globals->dram_size);
	if (err < 0) {
		printf("Can't set memory range\n");
		return false;
	}

	if (boot->loaded_images & (1 << IMAGE_INITRD)) {
		uint32_t initramfs_start = boot->image_dests[IMAGE_INITRD];
		uint32_t initramfs_end = initramfs_start + boot->image_sizes[IMAGE_INITRD];

		err = fdt_initrd(fdt, initramfs_start, initramfs_end);
		if (err < 0) {
			printf("Can't setup initrd\n");
			return false;
		}
	}

	// this also sets up reservation for FDT blob
	err = fdt_shrink_to_minimum(fdt, 0);
	if (err < 0) {
		printf("Can't shrink fdt\n");
		return false;
	}

	return true;
}

static inline void raw_write_daif(unsigned int daif)
{
        __asm__ __volatile__("msr DAIF, %0\n\t" : : "r" (daif) : "memory");
}

static void jump_to_atf(void)
{
	void (*atf_entry_point)(struct entry_point_info *ep_info,
				void *fdt_blob, uintptr_t magic);
	/*
	 * Holds information passed to ATF about where to jump after ATF
	 * finishes.
	 */
	static struct entry_point_info* bl33_ep_info;

	printf("%d us: jumping to ATF\n", timer_get_boot_us() - globals->t0);

	bl33_ep_info = zalloc(sizeof *bl33_ep_info);

	SET_PARAM_HEAD(bl33_ep_info, ATF_PARAM_EP, ATF_VERSION_1,
		       ATF_EP_NON_SECURE);

        /* BL33 (Linux) expects to receive FDT blob through x0 */
	bl33_ep_info->args.arg0 = FDT_BLOB_PA;
	bl33_ep_info->pc = globals->linux_image_pa;
	bl33_ep_info->spsr = SPSR_64(MODE_EL2, MODE_SP_ELX,
				     DISABLE_ALL_EXECPTIONS);

	//disable_interrupts();
	icache_disable();
	invalidate_icache_all();
	dcache_disable();
	invalidate_dcache_all();

	raw_write_daif(SPSR_EXCEPTION_MASK);

	atf_entry_point = (void*)(uintptr_t)ATF_PA;
	atf_entry_point((void *)bl33_ep_info, (void *)(uintptr_t)FDT_BLOB_PA, 0xb001);

	panic(5, "ATF entry failed\n");
}

void boot_perform(struct boot* boot)
{
	// mark end of p-boot by switching to red led
        green_led_set(0);
        red_led_set(1);

	lradc_disable();
	wdog_disable();
	jump_to_atf();
}

// }}}

void panic_shutdown(uint32_t code)
{
        // blink green led in a binary pattern

        green_led_set(0);
        for (int i = 0; i <= 16; i++) {
                red_led_set(i % 2);
                udelay(100000);
        }

        udelay(500000);

        for (int i = 0; i < 5; i++) {
                int set = code & (1 << (4 - i));

                green_led_set(1);
                udelay(set ? 500000 : 100000);

                green_led_set(0);
                udelay(set ? 500000 : 900000);
        }

        red_led_set(1);
        udelay(500000);

	puts("Power off!\n");
	pmic_poweroff();
}

void main_sram_only(void)
{
	int ret;
	uint64_t t0 = timer_get_boot_us();

	green_led_set(1);
	ccu_init();
	console_init();
	lradc_enable();
	wdog_ping();

	puts("p-boot (version " VERSION " built " BUILD_DATE ")\n");

        // init PMIC first so that panic_shutdown() can power off the board
        // in case of panic()
	ret = rsb_init();
	if (ret)
		panic(6, "RSB failed %d\n", ret);

	pmic_init();
	printf("PMIC ready\n");

	uint64_t dram_size = sunxi_dram_init();
	if (!dram_size)
		panic(3, "DRAM not detected");

	// 256MiB from end of DRAM is our heap
	uint8_t* heap_end = (void*)(CONFIG_SYS_SDRAM_BASE + dram_size - 256 * 1024 * 1024);
	globals = (void*)heap_end;
	heap_end += ALIGN(sizeof *globals, 64);
	memset(globals, 0, sizeof *globals);

	globals->t0 = t0;
	globals->heap_end = heap_end;
	globals->dram_size = dram_size;

	icache_enable();
	mmu_setup(dram_size);

#ifdef VIDEO_CONSOLE
	sys_console = zalloc(sizeof *sys_console);
	vidconsole_init(sys_console, 45, 45, 2, 0xffffffff, 0x00000000);
#endif

#ifdef PBOOT_FDT_LOG
	// allocate 128 KiB for the log and move it to DRAM from SRAM C
	char* new_log = malloc(128 * 1024);
	unsigned log_size = tmp_log_end - TMP_LOG_START;
	memcpy(new_log, TMP_LOG_START, log_size);
	globals->log_end = globals->log_start = new_log;
	globals->log_end += log_size;

#ifdef VIDEO_CONSOLE
	// feed the sys console
	for (char* c = globals->log_start; c < globals->log_end; c++)
		vidconsole_putc(sys_console, *c);
#endif
#endif

	printf("%d us: DRAM: %u MiB\n", timer_get_boot_us() - t0,
	       (unsigned)(dram_size >> 20));

	ccu_upclock();

	//XXX: set CPU voltage to high and increase the clock to the highest OPP
	// causes intermittent SD/emmc init/load failures
	//
	// udelay(1000);
	// clock_set_pll1(1152000000);

	void* dram_stack = malloc(128 * 1024);
	extern uint32_t _dram_stack_top;
	_dram_stack_top = (uintptr_t)((uint8_t*)dram_stack + 128 * 1024);
}

static void boot_selection(struct bootfs* fs, struct bootfs_conf* sbc, uint32_t splash_fb)
{
	//
	// Normal boot
	//

	struct boot* boot = zalloc(sizeof *boot);
	if (!boot_prepare(boot, fs, sbc))
		panic(12, "Failed to load boot images\n");

	if (splash_fb)
		fdt_setup_framebuffer(boot, splash_fb);

	//boot_append_bootargs(boot, globals->mmc_no == 0 ? "bootdev=sd" : "bootdev=emmc");

	void* fdt = boot->fdt;

	// need to remove x-powers,ts-as-gpadc from FDT, because p-boot
	// configures TS correctly and we want battery thermal protection
	// to function correctly
        int adc_node = fdt_path_offset(fdt, "/soc/rsb@1f03400/pmic@3a3/adc");
	if (adc_node >= 0)
		fdt_delprop(fdt, adc_node, "x-powers,ts-as-gpadc");

        const char* model = fdt_getprop(fdt, 0, "model", NULL);
        if (model)
                printf("DT model: %s\n", model);

	if (!boot_finalize(boot))
		panic(13, "Failed to finalize boot\n");

	boot_perform(boot);
}

static void reboot_to(int bootsel)
{
	if (bootsel == BOOTSEL_FEL) {
		// reboot to FEL
		writel(1, SUNXI_RTC_BASE + 0x104);
		soc_reset();
	} else if (bootsel == BOOTSEL_EMMC) {
		// reboot to eMMC
		writel(2, SUNXI_RTC_BASE + 0x104);
		soc_reset();
	}
}

static bool load_splash(struct bootfs* fs, struct bootfs_conf* bc, uint32_t dest)
{
	if (memcmp(bc->magic, ":BFCONF:", 8))
		return false;

	for (int j = 0; j < ARRAY_SIZE(bc->images); j++) {
		struct bootfs_image* im = &bc->images[j];
		unsigned type = __be32_to_cpu(im->type);
		if (type == 'S') {
			ssize_t size = bootfs_load_image(fs, dest,
					  __be32_to_cpu(im->data_off),
					  __be32_to_cpu(im->data_len),
					  "splash");
			return size > 0;
		}
	}

	return false;
}

static const char* get_boot_source_name(void)
{
	switch (globals->boot_source) {
		case SUNXI_BOOTED_FROM_MMC0: return "SD";
		case SUNXI_BOOTED_FROM_MMC2: return "eMMC";
		case SUNXI_BOOTED_FROM_FEL: return "FEL";
		default: return "?";
	}
}

#define COLOR_NORMAL_ITEM 0xffeeccdd
#define COLOR_DEFAULT_ITEM 0xff11ff22
#define COLOR_SD_HEADER 0xffff8569
#define COLOR_EMMC_HEADER 0xffa0c5ff
#define COLOR_POWEROFF_ITEM 0xffff2211
#define COLOR_FEL_ITEM 0xffee77ff
#define COLOR_HEADER 0xffddee77
#define COLOR_FOOTER 0xffddee77
#define COLOR_FOOTER_BAD 0xffff1122

static void gui_menu_set_active(struct gui_menu* m, int id)
{
	for (int i = 0; i < m->n_items; i++)
		if (m->items[i].id < 67)
			m->items[i].active = m->items[i].id == id;

	m->changed = true;
}

static void boot_gui(void)
{
	struct display* d = zalloc(sizeof *d);
	struct gui* g = zalloc(sizeof *g);
	struct bootsel rtcsel;
	struct bootfs* fs = globals->emmc ? globals->emmc : globals->sd;

	wdog_disable();

	// background image
	bootfs_load_file(fs, 0x48000000, "pboot2.argb");
	d->planes[0].fb_start = 0x48000000;
	d->planes[0].fb_pitch = 720 * 4;
	d->planes[0].src_w = 720;
	d->planes[0].src_h = 1440;
	d->planes[0].dst_w = 720;
	d->planes[0].dst_h = 1440;

        gui_init(g, d);
	display_commit(g->display);
	rtc_bootsel_get(&rtcsel);

	struct gui_menu* m = gui_menu(g);

	if (globals->emmc || globals->boot_source != SUNXI_BOOTED_FROM_MMC2) {
		gui_menu_add_item(m, -2, "eMMC:", COLOR_EMMC_HEADER, COLOR_EMMC_HEADER);
		for (int i = 0; i < 32 && globals->emmc; i++) {
			struct bootfs_conf* c = &globals->emmc->confs_blocks[i];
			if (!memcmp(c->magic, ":BFCONF:", 8))
				gui_menu_add_item(m, i, (char*)c->name, COLOR_NORMAL_ITEM, COLOR_DEFAULT_ITEM);
		}

		if (globals->boot_source != SUNXI_BOOTED_FROM_MMC2)
			gui_menu_add_item(m, BOOTSEL_EMMC, "Boot via bootloader on eMMC", COLOR_NORMAL_ITEM, COLOR_DEFAULT_ITEM);

		gui_menu_add_item(m, -2, "", 0, 0);
	}

	if (globals->sd) {
		gui_menu_add_item(m, -2, "SD:", COLOR_SD_HEADER, COLOR_SD_HEADER);
		for (int i = 0; i < 32; i++) {
			struct bootfs_conf* c = &globals->sd->confs_blocks[i];
			if (!memcmp(c->magic, ":BFCONF:", 8))
				gui_menu_add_item(m, i + 32, (char*)c->name, COLOR_NORMAL_ITEM, COLOR_DEFAULT_ITEM);
		}

		gui_menu_add_item(m, -2, "", 0, 0);
	}

	//gui_menu_add_item(m, 100, "Console ->", 0xff770011);
	gui_menu_add_item(m, BOOTSEL_FEL, "Reboot to FEL", COLOR_FEL_ITEM, COLOR_DEFAULT_ITEM);
	gui_menu_add_item(m, BOOTSEL_POWEROFF, "Poweroff", COLOR_POWEROFF_ITEM, COLOR_POWEROFF_ITEM);

	if (rtcsel.def >= 0) {
		gui_menu_set_active(m, rtcsel.def);
		gui_menu_set_selection(m, rtcsel.def);
	} else {
		gui_menu_set_selection(m, !globals->emmc ? 32 : 0);
	}

	const char* title = "Boot menu";
	if (fs->sb->device_id[0]) {
		fs->sb->device_id[sizeof(fs->sb->device_id) - 1] = 0;
		title = (const char*)fs->sb->device_id;
	}
	gui_menu_set_title(m, POS_TOP_LEFT, title, COLOR_HEADER, 0x55000000);

	struct strbuf* footer = strbuf_new(64);
	strbuf_printf(footer, "p-boot %s, %s ", VERSION, get_boot_source_name());

	uint32_t footer_color = (
		globals->bat_present
		&& !globals->bat_safe_mode
		&& !globals->uvlo_shutdown
		&& globals->bat_capacity > 30) ?
		COLOR_FOOTER : COLOR_FOOTER_BAD;

	if (globals->bat_present) {
		if (globals->bat_safe_mode)
			strbuf_putc(footer, 'S');
		if (globals->uvlo_shutdown)
			strbuf_putc(footer, 'U');

		if (globals->bat_capacity >= 0) {
			strbuf_putc(footer, 0x18 - !!globals->bat_charging);
			strbuf_putc(footer, 0x13 + globals->bat_capacity * 3 / 100);
			strbuf_printf(footer, " %u%%", globals->bat_capacity);
		}
	} else {
		strbuf_printf(footer, "NO BAT");
	}

	gui_menu_set_title(m, POS_BOTTOM_LEFT, strbuf_to_cstr(footer), footer_color, 0x55000000);

	int flips = 0;
	int boot_sel = -1;
	enum {
		STATE_MENU,
		STATE_OFF,
		STATE_BOOT,
	} state = STATE_MENU;

	uint64_t end = timer_get_boot_us() + 10000000;
	while (true) {
		gui_get_events(g);

		if (g->events & BIT(EV_VBLANK)) {
			// handle state switch after vblank
			if (state == STATE_OFF) {
				bootfs_load_file(fs, 0x48000000, "off.argb");
				d->planes[1].fb_start = 0;
				display_commit(g->display);
				udelay(1000000);
				pmic_poweroff();
				//soc_reset();
			} else if (state == STATE_BOOT) {
				struct bootfs* cfs = boot_sel < 32 ? globals->emmc : globals->sd;
				struct bootfs_conf* c = &cfs->confs_blocks[boot_sel % 32];

				load_splash(cfs, c, 0x48000000);
				d->planes[1].fb_start = 0;
				display_commit(g->display);
				gui_fini(g);
				boot_selection(cfs, c, 0x48000000);
				soc_reset();
			}

			display_commit(g->display);
			flips++;

			if (flips == 1) {
				backlight_enable(60);
			}
		}

		// extend deadline if user is interacting
		if (g->events & ~BIT(EV_VBLANK))
			end = timer_get_boot_us() + 10000000;

		if (end < g->now) {
			state = STATE_OFF;
		}

		if (m->selection_changed) {
			int id = gui_menu_get_selection(m);
			if (id == BOOTSEL_POWEROFF) {
				bootfs_load_file(fs, 0x48000000, "off.argb");
			} else if (id < 64) {
				struct bootfs* cfs = id < 32 ? globals->emmc : globals->sd;
				struct bootfs_conf* c = &cfs->confs_blocks[id % 32];

				if (!load_splash(cfs, c, 0x48000000))
					bootfs_load_file(fs, 0x48000000, "pboot2.argb");
			}
		}

		if (g->events & BIT(EV_POK_LONG)) {
			// mark current selection as default
			int id = gui_menu_get_selection(m);
			if (id == BOOTSEL_POWEROFF) {
				rtcsel.def = -1;
				rtcsel.next = -1;
			} else {
				rtcsel.def = id;
			}
			rtc_bootsel_set(&rtcsel);

			gui_menu_set_active(m, rtcsel.def);
		}

		if (g->events & BIT(EV_POK_SHORT)) {
			int id = gui_menu_get_selection(m);

			reboot_to(id);

			if (id == BOOTSEL_POWEROFF) {
				state = STATE_OFF;
			} else if (id < 64) {
				state = STATE_BOOT;
				boot_sel = id;
			}
		}
	}

	gui_fini(g);
}

void main(void)
{
	struct bootfs* fs = NULL;
	int key;

	globals->board_rev = detect_pinephone_revision();
	globals->boot_source = get_boot_source();
	get_soc_id(globals->sid);

	pmic_setup_bat_temp_sensor();
	pmic_setup_ocv_table();
	pmic_read_status();

	printf("Boot Source: %s\n", get_boot_source_name());

	/* read volume keys status */
	udelay(12000);
	key = lradc_get_pressed_key();

#ifdef ENABLE_GUI
	if (key == KEY_VOLUMEUP)
		reboot_to(BOOTSEL_FEL);

	// show UI if any of the volume keys was pressed
	if (key == KEY_VOLUMEDOWN || key == KEY_VOLUMEUP || globals->vbus_powerup)
		goto display_init;

	int bootsel;
	struct bootfs_conf* sbc = bootfs_select_configuration(&bootsel, &fs);
	if (!sbc) {
		// handle special boot options
		reboot_to(bootsel);

		goto display_init;
	}

	// try to load splashscreen, if successful, init display to show it
	if (load_splash(fs, sbc, 0x48000000)) {
		// show splash
		display_init();

		struct display* d = zalloc(sizeof *d);
		d->planes[0].fb_start = 0x48000000;
		d->planes[0].fb_pitch = 720 * 4;
		d->planes[0].src_w = 720;
		d->planes[0].src_h = 1440;
		d->planes[0].dst_w = 720;
		d->planes[0].dst_h = 1440;
		display_commit(d);

		while (!display_frame_done());
		backlight_enable(60);

		boot_selection(fs, sbc, 0x48000000);
		goto boot_ui;
	} else {
		boot_selection(fs, sbc, 0);
	}

display_init:
	// we always try to probe and load everything in display mode
	display_init();
boot_ui:
	mmc_try_load(BIT(0) | BIT(2));
	boot_gui();
#else
	//
	// Boot priority:
	//
	// - if KEY_VOLUMEUP (used to force booting from SD card if possible)
	//   - option 0 from SD card
	//   - panic
	// - if KEY_VOLUMEDOWN (used to trigger alternate boot option)
	//   - option 1 from eMMC
	//   - option 1 from SD card
	// - if KEY_NONE
	//   - autoselect based on RTC data register (can select any boot option)
	// - config 0 from eMMC
	// - config 0 from SD card
	//
	struct bootfs_conf* sbc = NULL;
	int bootsel = 0;

	if (key == KEY_VOLUMEUP) {
		mmc_try_load(BIT(0));

		fs = globals->sd;
		sbc = fs ? &fs->confs_blocks[0] : NULL;
		if (sbc && !memcmp(sbc->magic, ":BFCONF:", 8))
			goto boot;

		goto nothing_to_boot;
	} else if (key == KEY_VOLUMEDOWN) {
		bootsel = 1;
	} else {
		sbc = bootfs_select_configuration(&bootsel, &fs);
		if (!sbc) {
			reboot_to(bootsel);
			bootsel = 0;
		}
	}

	// not selected yet?
try_conf0:
	if (!sbc) {
		mmc_try_load(BIT(2));

		fs = globals->emmc;
		sbc = fs ? &fs->confs_blocks[bootsel] : NULL;
		if (sbc && !memcmp(sbc->magic, ":BFCONF:", 8))
			goto boot;

		mmc_try_load(BIT(0));

		fs = globals->sd;
		sbc = fs ? &fs->confs_blocks[bootsel] : NULL;
		if (sbc && !memcmp(sbc->magic, ":BFCONF:", 8))
			goto boot;

		if (bootsel == 1) {
			bootsel = 0;
			goto try_conf0;
		}

		goto nothing_to_boot;
	}

boot:
	boot_selection(fs, sbc, 0);
nothing_to_boot:
	panic(11, "Nothing to boot");
#endif
	panic(10, "Should not return\n");
}
