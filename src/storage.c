#include "storage.h"

// {{{ U-Boot MMC driver wrapper

/* This is called from sunxi_mmc_init. */

struct mmc* mmc_create(const struct mmc_config *cfg, void *priv)
{
	struct mmc* mmc = zalloc(sizeof *mmc);
	struct blk_desc *bdesc;

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

struct mmc* mmc_probe(int mmc_no)
{
	struct mmc* mmc;
	unsigned int pin;
	const char* name = mmc_no ? "eMMC" : "SD";
	int ret;

	if (mmc_no == 0) {
		/* SDC0: PF0-PF5 */
		for (pin = SUNXI_GPF(0); pin <= SUNXI_GPF(5); pin++) {
			sunxi_gpio_set_cfgpin(pin, SUNXI_GPF_SDC0);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}
	} else {
		/* SDC2: PC5-PC6, PC8-PC16 */
		for (pin = SUNXI_GPC(5); pin <= SUNXI_GPC(16); pin++) {
			if (pin == SUNXI_GPC(7))
				continue;
			sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_SDC2);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}
	}

	mmc = sunxi_mmc_init(mmc_no);
	if (!mmc)
		goto err;

	ret = mmc_init(mmc);
	if (ret < 0)
		goto err;

	//printf("%d us: %s ready\n", timer_get_boot_us() - globals->t0, name);
	return mmc;

err:
	printf("Can't init %s\n", name);
	return NULL;
}

/* read data from eMMC to memory, length will be rounded to the block size (512B) */
bool mmc_read_data(struct mmc* mmc, uintptr_t dest, uint64_t off, uint32_t len)
{
	unsigned long sectors, sectors_read;

	sectors = (len + mmc->read_bl_len - 1) / mmc->read_bl_len;
	sectors_read = blk_dread(mmc_get_blk_desc(mmc), off / mmc->read_bl_len,
				 sectors, (void*)dest);

	return sectors == sectors_read;
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

struct bootfs* bootfs_open(struct mmc* mmc)
{
	int part_count = 0;
	struct part_info* parts = malloc(4 * sizeof(*parts));
	uint8_t* buf = malloc(512);
	struct bootfs* fs = NULL;

        // collect a list of partitions

	if (!mmc_read_data(mmc, (uintptr_t)buf, 0, 512))
		return NULL;

	if (buf[0x1fe] != 0x55 || buf[0x1ff] != 0xaa)
		return NULL;

	struct dos_partition *p = (struct dos_partition *)&buf[0x1be];
	for (int slot = 0; slot < 4; ++slot, ++p) {
		if (p->boot_ind != 0 && p->boot_ind != 0x80)
			return NULL;

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

	// go through partitions list twice (once through boot partitions, then
	// through others)

	fs = malloc(sizeof *fs);
	fs->mmc = mmc;
	fs->sb = malloc(2048 * 33);
	fs->confs_blocks = (void*)(fs->sb + 1);
	fs->files_blocks = (void*)(fs->sb + 1);

	printf("Searching for bootfs:\n");
	for (unsigned i = 0; i < part_count * 2; i++) {
		unsigned try = parts[i].is_boot ^ (i / part_count);
		struct part_info* pi = &parts[i % part_count];

		if (try)
			printf("  %s:%d %spart. (%llu MiB)\n",
			       mmc->cfg->name, pi->idx,
			       pi->is_boot ? "boot " : "",
			       pi->size / 1024 / 1024);

		if (try && mmc_read_data(mmc, (uintptr_t)fs->sb, pi->start, 33 * 2048) &&
		    !memcmp(fs->sb->magic, ":BOOTFS:", 8)) {
			fs->mmc_offset = pi->start;
			return fs;
		}
	}


	return NULL;
}

ssize_t bootfs_load_image(struct bootfs* fs, uint32_t dest, uint64_t off, uint32_t len, const char* name)
{
	if (len == 0)
		return 0;
	if (off % 512)
		return -1;
	if (len > 512 * 1024 * 1024)
		return -1;

	if (dest == 0)
		return len;

	ulong s = timer_get_boot_us();

	if (!mmc_read_data(fs->mmc, dest, fs->mmc_offset + off, len))
		return -1;

	printf("Load %s (%u KiB) => 0x%x (%llu KiB/s)\n",
	       name, len / 1024, dest,
	       (uint64_t)len * 1000000 / (timer_get_boot_us() - s) / 1024);

	return len;
}

ssize_t bootfs_load_file(struct bootfs* fs, uint32_t dest, const char* name)
{
	struct bootfs_files* bf = fs->files_blocks;

	for (int i = 0; i < 32; i++) {
		if (memcmp(bf[i].magic, ":BFILES:", 8))
			continue;

		for (int j = 0; j < ARRAY_SIZE(bf[i].files); j++) {
			struct bootfs_file* f = &bf[i].files[j];

			if (!f->name[0])
                                break;

			if (strcmp((char*)f->name, name))
				continue;

			uint64_t img_off = __be32_to_cpu(f->data_off);
			uint32_t img_len = __be32_to_cpu(f->data_len);

			return bootfs_load_image(fs, dest, img_off, img_len, (char*)f->name);
		}
	}

	return -1;
}

// }}}
