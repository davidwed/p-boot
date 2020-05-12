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
#include <asm/arch/mmc.h>
#include <asm/arch/clock.h>
#include <asm/arch/spl.h>
#include <asm/arch/prcm.h>
#include <atf_common.h>
#include <mmc.h>
#include "debug.h"
#include "mmu.h"
#include "pmic.h"
#include "bootfs.h"
#include "lradc.h"
#include <build-ver.h>

//#define CONFIG_DEVMODE

// {{{ SRAM/DRAM layout

/*
 * SRAM/DRAM layout during boot:
 *
 * 0x00044000 - ATF
 * 0x40000000 - DRAM start
 * 0x40080000 - Linux Image
 * 0x44000000 - Bootfs superblock + configuration table
 * 0x4a000000 - DTB
 * 0x4fe00000 - initramfs
 *
 * ATF maps 0x4a000000 - 0x4c000000 for main u-boot binary. We don't have u-boot
 * binary, but we want to place FDT there so that ATF can read it, without
 * setting up any new mappings. ATF needs to be patched to find the FDT there.
 */

#define ATF_PA		0x44000
#define LINUX_IMAGE_PA	0x40080000
#define BOOTFS_SB_PA	0x44000000
#define FDT_BLOB_PA	0x4a000000
#define INITRAMFS_PA	0x4fe00000

// }}}
// {{{ Globals

__attribute__((section(".lowdata")))
static ulong t0;

// }}}
// {{{ Simple heap implementation

static uint8_t* heap_end;

void* malloc(size_t len)
{
	void* p = heap_end;
	heap_end += ALIGN(len, 64);
	return p;
}

void free(void* p)
{
}

// }}}
// {{{ U-Boot MMC driver wrapper

/* This is called from sunxi_mmc_init. */

struct mmc* mmc_create(const struct mmc_config *cfg, void *priv)
{
	struct mmc* mmc = malloc(sizeof *mmc);
	struct blk_desc *bdesc;

	memset(mmc, 0, sizeof *mmc);

	/* quick validation */
	if (cfg == NULL || cfg->f_min == 0 ||
	    cfg->f_max == 0 || cfg->b_max == 0)
		return NULL;

	if (cfg->ops == NULL || cfg->ops->send_cmd == NULL)
		return NULL;

	mmc->cfg = cfg;
	mmc->priv = priv;

	/* Setup dsr related values */
	mmc->dsr_imp = 0;
	mmc->dsr = 0xffffffff;

	/* Setup the universal parts of the block interface just once */
	bdesc = mmc_get_blk_desc(mmc);
	bdesc->if_type = IF_TYPE_MMC;
	bdesc->removable = 1;
	bdesc->mmc = mmc;
	bdesc->block_read = mmc_bread;

	/* setup initial part type */
	bdesc->part_type = mmc->cfg->part_type;

	return mmc;
}

struct blk_desc *mmc_get_blk_desc(struct mmc *mmc)
{
	return &mmc->block_dev;
}

static struct mmc* mmc_init_sd(void)
{
	struct mmc* mmc;
	unsigned int pin;
	int ret;

	/* SDC0: PF0-PF5 */
	for (pin = SUNXI_GPF(0); pin <= SUNXI_GPF(5); pin++) {
		sunxi_gpio_set_cfgpin(pin, SUNXI_GPF_SDC0);
		sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
		sunxi_gpio_set_drv(pin, 2);
	}

	mmc = sunxi_mmc_init(0);
	if (!mmc)
		panic(1, "can't init mmc0\n");

	ret = mmc_init(mmc);
	if (ret < 0)
		panic(2, "can't init mmc0\n");

	printf("%d us: SD card ready\n", timer_get_boot_us() - t0);

	return mmc;
}

static struct mmc* mmc_init_emmc(void)
{
	struct mmc* mmc;
        unsigned int pin;
	int ret;

	/* SDC2: PC5-PC6, PC8-PC16 */
	for (pin = SUNXI_GPC(5); pin <= SUNXI_GPC(16); pin++) {
		if (pin == SUNXI_GPC(7))
			continue;
		sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_SDC2);
		sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
		sunxi_gpio_set_drv(pin, 2);
	}

	mmc = sunxi_mmc_init(2);
	if (!mmc)
		panic(3, "can't create mmc2\n");

	ret = mmc_init(mmc);
	if (ret < 0)
		panic(4, "can't init mmc2\n");

	printf("%d us: eMMC ready\n", timer_get_boot_us() - t0);

	return mmc;
}

/* read data from eMMC to memory, length will be rounded to the block size (512B) */
static bool mmc_read_data(struct mmc* mmc, uintptr_t dest,
			  uint64_t off, uint32_t len)
{
	unsigned long sectors, sectors_read;

	sectors = (len + mmc->read_bl_len - 1) / mmc->read_bl_len;
	sectors_read = blk_dread(mmc_get_blk_desc(mmc), off / mmc->read_bl_len,
				 sectors, (void*)dest);

	return sectors == sectors_read;
}

// }}}
// {{{ ATF entry/exit helpers

/*
 * Return point that will be jumped to from ATF.
 *
 * ATF will clobber anything in SRAM A1 above 0x1000, so we need to put
 * this function, all functions this calls, and all static data this uses
 * into .textlow and .lowdata sections.
 */
#ifndef CONFIG_ATF_TO_LINUX
__attribute__((section(".textlow")))
void atf_exit_finish(void)
{
	printf("%d us: ATF done, booting Linux...\n", timer_get_boot_us() - t0);

	armv8_switch_to_el2(FDT_BLOB_PA, 0, 0, 0,
			    LINUX_IMAGE_PA, ES_TO_AARCH64);

	panic(5, "BAD\n");
}

/* defined in start.S */
extern void atf_exit(void);
#endif

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

	printf("%d us: jumping to ATF\n", timer_get_boot_us() - t0);

	bl33_ep_info = malloc(sizeof *bl33_ep_info);
	memset(bl33_ep_info, 0, sizeof *bl33_ep_info);

	SET_PARAM_HEAD(bl33_ep_info, ATF_PARAM_EP, ATF_VERSION_1,
		       ATF_EP_NON_SECURE);

#ifdef CONFIG_ATF_TO_LINUX
        /* BL33 (Linux) expects to receive FDT blob through x0 */
	bl33_ep_info->args.arg0 = FDT_BLOB_PA;
	bl33_ep_info->pc = LINUX_IMAGE_PA;
	bl33_ep_info->spsr = SPSR_64(MODE_EL2, MODE_SP_ELX,
				     DISABLE_ALL_EXECPTIONS);
#else
	bl33_ep_info->pc = (uintptr_t)atf_exit;
	bl33_ep_info->spsr = SPSR_64(MODE_EL2, MODE_SP_ELX,
				     DISABLE_ALL_EXECPTIONS);
#endif

	//disable_interrupts();
	icache_disable();
	invalidate_icache_all();
	dcache_disable();
	invalidate_dcache_all();

	raw_write_daif(SPSR_EXCEPTION_MASK);

	atf_entry_point = (void*)(uintptr_t)ATF_PA;
	atf_entry_point((void *)bl33_ep_info, (void *)(uintptr_t)FDT_BLOB_PA, 0xb001);

	panic(6, "ATF entry failed\n");
}

// }}}
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
// {{{ DRAM

ulong dram_size;

static void dram_speed_test(unsigned int mb)
{
#ifdef CONFIG_DRAM_SPEED_TEST
	volatile uint64_t* dram = (void*)0x45000000;
	ulong t0, t1, t2;

	t0 = timer_get_boot_us();

	for (unsigned i = 0; i < 1024 * 1024 * mb / 8; i++)
		dram[i] = i;

	t1 = timer_get_boot_us();

	for (unsigned i = 0; i < 1024 * 1024 * mb / 8; i++)
		if (unlikely(dram[i] != i))
			panic(7, "DRAM test failed\n");

	t2 = timer_get_boot_us();

	printf("DRAM speed: wr=%llu rd=%llu\n", t1 - t0, t2 - t1);
#endif
}

static void dram_init(void)
{
	dram_size = sunxi_dram_init();
	if (!dram_size)
		panic(8, "DRAM not detected");

	// 256MiB from end of DRAM is our heap
	heap_end = (void*)(CONFIG_SYS_SDRAM_BASE + dram_size - 1024*1024*256);

	icache_enable();
	mmu_setup(dram_size);

	printf("%d us: DRAM: %u MiB\n", timer_get_boot_us() - t0,
	       (unsigned)(dram_size >> 20));

	dram_speed_test(32);
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
	uint32_t sid[4];

	get_soc_id(sid);

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
// {{{ PMIC initialization

static void pmic_init(void)
{
	int ret;

	ret = rsb_init();
	if (ret)
		panic(9, "rsb init failed %d\n", ret);

	printf("%d us: PMIC ready\n", timer_get_boot_us() - t0);

#ifdef DEBUG
	printf("Dumping PMIC registers:");
	for (int i = 0; i < 0x80; i++)
		printf("%x: %x\n", i, axp_read(i));
#endif

        // enable DCDC/PWM chg freq spread
	axp_write(0x3b, 0x88);

        // up the DCDC2 voltage to 1.3V (CPUX)
        // default is 0.9V, and rampup speed is 2.5mV/us
        // so we need 400mV/2.5mV = 160us before being able to ramp up
        // CPU frequency
        axp_write(0x21, 0x4b);

#ifdef CONFIG_PMIC_VERBOSE
        int status0, status1, status2;

	// read status registers
	status0 = axp_read(0x00);
	status1 = axp_read(0x01);
	status2 = axp_read(0x02);

	// clear power up status
	axp_write(0x02, 0xff);

	// print PMIC status
	if (status0 >= 0 && status1 >= 0 && status2 >= 0) {
		if (status2 & BIT(0))
			printf("  PMIC power up by POK\n");
		if (status2 & BIT(1))
			printf("  PMIC power up by USB power\n");
		if (status2 & BIT(5))
			printf("  PMIC UVLO!\n");

		printf("  VBUS %s\n", status0 & BIT(5) ? "present" : "absent");

		if (status1 & BIT(5) && status1 & BIT(4)) {
			printf("  Battery %s3.5V\n", status0 & BIT(3) ? ">" : "<");
			printf("  Battery %s\n", status0 & BIT(2) ? "charging" : "discharging");
			if (status1 & BIT(3))
				printf("  Battery in SAFE mode\n");
		} else {
			printf("  Battery absent\n");
		}
	}
#else
	// clear power up status
	axp_write(0x02, 0xff);
#endif

	// disable temp sensor charger effect
	axp_setbits(0x84, BIT(2));

	// when SDP not detected set 2A VBUS current limit (my charger can do that)
	axp_write(0x30, 0x02);

	// enable charger detection
	axp_write(0x2c, 0x95);

	// short POK reaction times
	axp_write(0x36, 0x08);

        // start battery max capacity calibration
        axp_setbits(0xb8, BIT(5));
}

static void pmic_poweroff(void)
{
        // power off via PMIC
	axp_setbits(0x32, BIT(7));
        hang();
}

static void pmic_reboot(void)
{
	// soft power restart via PMIC
	axp_setbits(0x31, BIT(6));
        hang();
}

static void pmic_enable_pinephone_lcd_power(void)
{
	// dldo1 3.3V
	axp_write(0x15, 0x1a);
	axp_setbits(0x12, BIT(3));

	// dldo2 3.3V
//	axp_write(0x16, 0x1a);
//	axp_setbits(0x12, BIT(4));

	// ldo_io0 3.3V
//	axp_write(0x91, 0x1a); // set LDO voltage to 3.3V
//	axp_write(0x90, 0x03); // enable LDO mode on GPIO0
}

static void pmic_setup_bat_temp_sensor(void)
{
	// setup TS
	// 12 bits ADC output code = R_NTC(Ω) * REG 84[5:4]( μA) / (0.8 * 1000).
	// REG 84H [5:4] 00: 20uA; 01: 40uA; 10: 60uA; 11: 80uA (11 default)
	//         [1:0] 10: on in ADC phase and off when out of the ADC phase, for power saving;
	// REG 82h [0] TS pin input to ADC enable - 1=on
	// REG 58_[7:0] REG 59_[3:0]  (raw value from TS adc)
	// REG 84_[2] -- allow to control the charger
	// Vhtf Vltf
	// REG 38H: V LTF-charge settin   M*10H,M=A5H: 2.112V; range is 0V-3.264V
	// REG 39H: V HTF-charge setting  N*10H,N=1FH: 0.397V; range is 0V-3.264V
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
                axp_write(0xc0 + i, ocv_tab[i]);
}

static void pmic_write_data(unsigned off, uint8_t data)
{
	if (off > 11)
		return;

	// data registers inside PMIC
	axp_write(0x04 + off, data);
}

static int pmic_read_data(unsigned off)
{
	if (off > 11)
		return -1;

	// data registers inside PMIC
	return axp_read(0x04 + off);
}

static void pmic_reboot_with_timer(void)
{
	// enable power up by IRQ source
	// detect irq wakeup by 8f[7]
	//axp_setbits(0x31, BIT(3));
	//axp_write(0x8a, 0x83); // start timer for 3 units
	// 0x4c BIT(7) - event timer interrupt flag, wr 1 to clear
	// 0x44 BIT(7) - event timer int en
}

// }}}
// {{{ Bootfs helpers

struct dos_partition {
	uint8_t boot_ind;         /* 0x80 - active                        */
	uint8_t head;             /* starting head                        */
	uint8_t sector;           /* starting sector                      */
	uint8_t cyl;              /* starting cylinder                    */
	uint8_t sys_ind;          /* What partition type                  */
	uint8_t end_head;         /* end head                             */
	uint8_t end_sector;       /* end sector                           */
	uint8_t end_cyl;          /* end cylinder                         */
	uint32_t start;           /* starting sector counting from 0      */
	uint32_t size;            /* nr of sectors in partition           */
};

struct part_info {
	uint64_t start;
	uint64_t size;
	bool is_boot;
	int idx;
};

static int bootsel;

static bool bootfs_sb_valid(void)
{
	struct bootfs_sb* sb = (struct bootfs_sb*)(uintptr_t)BOOTFS_SB_PA;

	return !memcmp(sb->magic, ":BOOTFS:", 8);
}

static bool bootfs_load(struct mmc* mmc, struct part_info* pi)
{
	printf("Trying %s partition located at 0x%llx(%llu MiB) (part %d)\n",
			pi->is_boot ? "boot" : "normal",
			pi->start, pi->size / 1024 / 1024, pi->idx);

	return mmc_read_data(mmc, BOOTFS_SB_PA, pi->start, 33 * 2048) && bootfs_sb_valid();
}

static bool bootfs_locate(struct mmc* mmc, uint64_t* offset)
{
	int part_count = 0;
	struct part_info* parts = malloc(4 * sizeof(*parts));

	if (!mmc_read_data(mmc, BOOTFS_SB_PA, 0, 512))
		return false;

	uint8_t* buf = (uint8_t*)(uintptr_t)BOOTFS_SB_PA;
	if (buf[0x1fe] != 0x55 || buf[0x1ff] != 0xaa)
		return false;

	struct dos_partition *p = (struct dos_partition *)&buf[0x1be];
	for (int slot = 0; slot < 4; ++slot, ++p) {
		if (p->boot_ind != 0 && p->boot_ind != 0x80)
			return false;

		if (p->sys_ind == 0x83) {
			struct part_info* pi = &parts[part_count++];

			pi->idx = slot;
			pi->start = 512ull * __le32_to_cpu(p->start);
			pi->size = 512ull * __le32_to_cpu(p->size);
			pi->is_boot = p->boot_ind == 0x80;
		}

		//XXX: extended partitions are not supported yet
		//if (p->sys_ind == 0x5 || p->sys_ind == 0xf || p->sys_ind == 0x85) {
		//}
	}

	for (int i = 0; i < part_count; i++) {
		if (parts[i].is_boot && bootfs_load(mmc, &parts[i])) {
			*offset = parts[i].start;
			return true;
		}
	}

	for (int i = 0; i < part_count; i++) {
		if (!parts[i].is_boot && bootfs_load(mmc, &parts[i])) {
			*offset = parts[i].start;
			return true;
		}
	}

	return false;
}

static struct bootfs_conf* bootfs_select_configuration(void)
{
	struct bootfs_sb* sb = (struct bootfs_sb*)(uintptr_t)BOOTFS_SB_PA;
	struct bootfs_conf* bc = (struct bootfs_conf*)(uintptr_t)(BOOTFS_SB_PA + 2048);

	if (!bootfs_sb_valid())
		panic(10, "BOOTFS not found");

	/* read volume keys status */
	int key = lradc_get_pressed_key();
	lradc_disable();

	/* select default boot configuration */
	bootsel = __be32_to_cpu(sb->default_conf);

	/* override boot configuration based on RTC data register */
        uint32_t rtc_data = readl((ulong)SUNXI_RTC_BASE + 0x100);
        if ((rtc_data & 0x7f) > 0) {
		bootsel = (rtc_data & 0x7f) - 1;
		/* reset the PMIC register if bit 7 is set */
                if (rtc_data & BIT(7))
                        writel(0, (ulong)SUNXI_RTC_BASE + 0x100);
		printf("Bootsel override via RTC data[0] to %d\n", bootsel);
        }

	/* override boot configuration based on PMIC data register */
	int reg = pmic_read_data(0);
	if (reg >= 0 && (reg & 0x7f) > 0) {
		bootsel = (reg & 0x7f) - 1;
		/* reset the PMIC register */
                if (reg & BIT(7))
                        pmic_write_data(0, 0);
		printf("Bootsel override via PMIC data[0] to %d\n", bootsel);
	}

	/* override boot config based on volume keys */
	if (key == KEY_VOLUMEDOWN)
		bootsel = 0;
	else if (key == KEY_VOLUMEUP)
		bootsel = 1;

	if (bootsel > 32)
		panic(11, "Bootsel out of range (%d)\n", bootsel);

	if (memcmp(bc[bootsel].magic, ":BFCONF:", 8)) {
		printf("Boot option %d not found\n", bootsel);
		bootsel = 1;
	}
	if (memcmp(bc[bootsel].magic, ":BFCONF:", 8)) {
		printf("Boot option %d not found\n", bootsel);
		bootsel = 0;
	}
	if (memcmp(bc[bootsel].magic, ":BFCONF:", 8)) {
		printf("Boot option %d not found\n", bootsel);
		panic(12, "Nothing to boot\n");
	}

	printf("%d us: Booting configuration %d (%s)\n",
	       timer_get_boot_us() - t0, bootsel, (char*)bc[bootsel].name);

	return &bc[bootsel];
}

// }}}
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

static char* fixup_bootargs(char* bootargs, bool is_sd)
{
	char *out, *end;
	int len;

	len = strlen(bootargs);
	out = malloc(len + 1000);
	memcpy(out, bootargs, len);
	end = out + len;

	memcpy(end, " bootdev=", 9);
	end += 9;
	if (is_sd) {
		memcpy(end, "sd", 2);
		end += 2;
	} else {
		memcpy(end, "emmc", 4);
		end += 4;
	}

	*end = 0;
	return out;
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
        panic(13, "SoC reset failed");
}

// }}}
// {{{ Detect PinePhone hardware

static int detect_pinephone_revision(void)
{
	int ret = 1;

	prcm_apb0_enable(PRCM_APB0_GATE_PIO);
	sunxi_gpio_set_pull(SUNXI_GPL(6), SUNXI_GPIO_PULL_UP);
	sunxi_gpio_set_cfgpin(SUNXI_GPL(6), SUNXI_GPIO_INPUT);

	udelay(100);

	/* PL6 is pulled low by the modem on v1.2. */
	if (!gpio_get_value(SUNXI_GPL(6)))
		ret = 2;

	sunxi_gpio_set_cfgpin(SUNXI_GPL(6), SUNXI_GPIO_DISABLE);
	sunxi_gpio_set_pull(SUNXI_GPL(6), SUNXI_GPIO_PULL_DISABLE);
	prcm_apb0_disable(PRCM_APB0_GATE_PIO);

	return ret;
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

#ifdef CONFIG_DEVMODE
	puts("Reset!\n");
	soc_reset();
#else
	puts("Power off!\n");
	pmic_poweroff();
#endif
}

void main(void)
{
	struct mmc* mmc;

	t0 = timer_get_boot_us();

	green_led_set(1);
	clock_init_safe();
	lradc_enable();
	debug_init();

	puts("\np-boot (version " VERSION " built " BUILD_DATE ")\n");

        // init PMIC first so that panic_shutdown() can power off the board
        // in case of panic()
	pmic_init();

	dram_init();
	clocks_init();

	uint32_t sid[4];
	get_soc_id(sid);
	printf("SoC ID: %08x:%08x:%08x:%08x\n", sid[0], sid[1], sid[2], sid[3]);

        rtc_fixup();

	// enable LCD power early (150ms powerup requirement happens while we read data from eMMC)
	pmic_enable_pinephone_lcd_power();
	pmic_setup_bat_temp_sensor();
	pmic_setup_ocv_table();

	// set CPU voltage to high and increase the clock to the highest OPP
	udelay(160);
	clock_set_pll1(1152000000);

	printf("Boot Source: %x\n", get_boot_source());

	uint64_t bootfs_offset = 0;
	enum {SD, MMC} source = MMC;

	// we always boot from eMMC, even when bootloader started from SD card
	// having p-boot on SD card speeds up boot by 1s (BROM wait time for eMMC)
	mmc = mmc_init_emmc();
	if (!mmc || !bootfs_locate(mmc, &bootfs_offset)) {
		printf("BOOTFS not found on eMMC, trying SD card\n");
		mmc = mmc_init_sd();
		source = SD;

		// read bootfs superblock and the config table
		if (!mmc || !bootfs_locate(mmc, &bootfs_offset))
			panic(14, "BOOTFS not found on SD card\n");
	}

	struct bootfs_conf* sbc = bootfs_select_configuration();

	// read the images from the selected table entry to memory
	ulong initramfs_start = 0, initramfs_end = 0;
	bool has_atf = false, has_linux = false;
	char* bootargs = (char*)sbc->boot_args;
	void* fdt_blob = NULL;

	for (int j = 0; j < sizeof(sbc->images) / sizeof(sbc->images[0]); j++) {
		struct bootfs_image* im = &sbc->images[j];
		unsigned type = __be32_to_cpu(im->type);
		uintptr_t dest = 0;

		switch (type) {
			case 'A':
				dest = ATF_PA;
				has_atf = true;
				break;
			case 'D':
				dest = FDT_BLOB_PA;
				fdt_blob = (void*)dest;
				break;
			case 'L':
				dest = LINUX_IMAGE_PA;
				has_linux = true;
				break;
			case 'I':
				dest = INITRAMFS_PA;
				initramfs_start = dest;
				initramfs_end = dest + __be32_to_cpu(im->data_len);
				break;
		}

		if (!dest)
			break;

		uint64_t img_off = (uint64_t)bootfs_offset + __be32_to_cpu(im->data_off);
		uint32_t img_len = __be32_to_cpu(im->data_len);
		if (img_off % 512)
			panic(15, "failed to read BFCONF[%d]IMG[%d]: unaligned offset\n", bootsel, j);
		if (img_len > 1024*1024*512)
			panic(16, "failed to read BFCONF[%d]IMG[%d]: image too big\n", bootsel, j);

		ulong s = timer_get_boot_us();
		if (!mmc_read_data(mmc, dest, img_off, img_len))
			panic(17, "failed to read BFCONF[%d]IMG[%d]\n", bootsel, j);

		printf("%d us: IMG[%c]: 0x%llx(%u B) -> 0x%x (%llu KiB/s)\n",
		       timer_get_boot_us() - t0, type,
		       img_off, img_len, (unsigned)dest,
		       (uint64_t)img_len * 1000000 / (timer_get_boot_us() - s) / 1024);
	}

	if (!has_atf)
		panic(18, "missing ATF(+SCP) image");
	if (!has_linux)
		panic(19, "missing Linux image");
	if (!fdt_blob)
		panic(20, "missing FDT blob\n");

        // setup FDT

        int err = fdt_check_header(fdt_blob);
        if (err < 0)
                panic(21, "bad FDT hedaer: %s\n", fdt_strerror(err));

        const char* model = fdt_getprop(fdt_blob, 0, "model", NULL);
        if (model)
                printf("Model: %s\n", model);

	err = fdt_increase_size(fdt_blob, 1024*32); // generous
        if (err < 0)
                panic(22, "can't expand FDT: %s\n", fdt_strerror(err));

        int chosen_off = fdt_find_or_add_subnode(fdt_blob, 0, "chosen");
        if (chosen_off < 0)
                panic(23, "can't create /chosen node\n");

	bootargs = fixup_bootargs(bootargs, source == SD);

	err = fdt_setprop(fdt_blob, chosen_off, "bootargs",
			  bootargs, strlen(bootargs) + 1);
	if (err < 0)
                panic(24, "can't set bootargs %d (%s)\n", err, bootargs);

	err = fdt_fixup_memory(fdt_blob, 0x40000000, dram_size);
	if (err < 0)
		panic(25, "can't set memory range\n")

	if (initramfs_start) {
		err = fdt_initrd(fdt_blob, initramfs_start, initramfs_end);
		if (err < 0)
			panic(26, "can't setup initrd\n");
	}

	fdt_fixup_wifi(fdt_blob);

	// PinePhone doesn't need BT local address fixup
	//fdt_fixup_bt(fdt_blob);

	// this also sets up reservation for FDT blob
	err = fdt_shrink_to_minimum(fdt_blob, 0);
	if (err < 0)
		panic(27, "can't shrink fdt\n")

	// mark end of p-boot by switching to red led
        green_led_set(0);
        red_led_set(1);

#ifdef CONFIG_DEVMODE
        puts("Reset!\n");
	soc_reset();
#endif

	jump_to_atf();
}
