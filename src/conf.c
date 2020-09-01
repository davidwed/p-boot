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

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <endian.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "bootfs.h"
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

/*
 * A tool for creating a bootfs filesystem.
 *
 * This is a simple read-only filesystem for p-boot, that contains file data in
 * continuous segments that can be read very quickly without any indirection.
 *
 * Metadata is stored in the first 64 KiB in 32 2 KiB blocks. There are 3 types
 * of metadata blocks:
 * - superblock: contains information about the filesystem as a whole (block 0)
 * - bconf: contains one boot configuration (blocks 1-31)
 * - files: contains a list of 51 file nodes (name + data offset/length) (blocks 1-31)
 *          filename size limit is 31 characters
 */

struct data {
	char path[PATH_MAX];
	int fd;
	uint32_t offset;
	uint32_t size;
	struct data* next;
};

struct file {
	char name[32];
	struct data* data;
};

struct bconf_image {
	uint32_t type;
	struct data* data;
	struct bconf_image* next;
};

struct bconf {
	int used;
	int index;
	char path[1024];
	char name[1024];
	char bootargs[4096];
	struct bconf_image* images;
};

static char* device_id;
static struct data* data_list;
static struct bconf confs[32];
static int n_files;
static struct file files[400];

// {{{ Parse conf file

static struct data* data_add_file(const char* path)
{
	struct data* d, *last_d;
	char rpath[PATH_MAX];

	if (!realpath(path, rpath)) {
		printf("ERROR: Can't resolve path '%s' (%s)", path, strerror(errno));
		exit(1);
	}

	for (d = data_list, last_d = d; d; last_d = d, d = d->next) {
		if (!strcmp(rpath, d->path))
			return d;
	}

	d = malloc(sizeof *d);
	assert(d != NULL);
	memset(d, 0, sizeof *d);

	int fd = open(rpath, O_RDONLY);
	if (fd < 0) {
		printf("ERROR: Can't open file '%s' (%s)", path, strerror(errno));
		exit(1);
	}

	//XXX: maybe read file contents and avoid dups based on content too

	snprintf(d->path, sizeof d->path, "%s", rpath);
        d->fd = fd;

	if (last_d)
		last_d->next = d;
	else
		data_list = d;

	return d;
}

static const struct image_type {
	const char* conf_var;
	char type;
	bool optional;
} image_types[] = {
	{ "linux",     'L', },
	{ "initramfs", 'I', true },
	{ "atf",       'A', },
	{ "dtb",       'D', },
	{ "splash",    'S', true },
};

static void complete_conf(struct bconf* c)
{
	if (confs[c->index].used) {
		printf("ERROR: %s: Configuration slot no=%d is already taken by '%s'\n", c->path, c->index, confs[c->index].path);
		exit(1);
	}

	if (!c->name[0]) {
		printf("ERROR: %s: Configuration slot no=%d is missing a name\n", c->path, c->index);
		exit(1);
	}

	for (int i = 0; i < sizeof(image_types) / sizeof(image_types[0]); i++) {
		if (image_types[i].optional)
			continue;

		struct bconf_image* im;
                for (im = c->images; im; im = im->next) {
			if (im->type == image_types[i].type)
				goto found;
		}

		printf("ERROR: %s: Configuration slot no=%d is missing '%s' image\n", c->path, c->index, image_types[i].conf_var);
		exit(1);
found:;
	}

	c->used = 1;
	confs[c->index] = *c;
}

static bool parse_conf(const char* conf_dir, const char* conf_filename)
{
	struct bconf conf_tpl = {};

	snprintf(conf_tpl.path, sizeof conf_tpl.path, "%s/%s", conf_dir, conf_filename);

	FILE* f = fopen(conf_tpl.path, "r");
	if (!f) {
		printf("ERROR: can't open %s", conf_tpl.path);
		exit(1);
	}

	char line[4096];
	struct bconf conf = conf_tpl;
	bool conf_started = false;
	int line_no = 0;

	while (fgets(line, sizeof line, f)) {
		char* end = line + strlen(line);
		while (--end >= line && *end == ' ' || *end == '\t' || *end == '\n')
			*end = 0;
                char* name = line;
                line_no++;

		while (*name == ' ' || *name == '\t')
			name++;

		if (*name == '#')
			continue;
		if (*name == '\0')
			continue;

		char* eq = strchr(name, '=');
		if (!eq) {
			printf("WARNING: %s[%d]: Skipping invalid line (%s)", conf.path, line_no, line);
			continue;
		}

		char* val = eq + 1;

		// trim name from the end
		do {
			*eq-- = 0;
		} while (eq >= name && (*eq == ' ' || *eq == '\t'));

		// trim value from the start
		while (*val == ' ' || *val == '\t')
			val++;

		if (val) {
			if (!strcmp(name, "device_id")) {
				if (device_id) {
					printf("ERROR: %s[%d]: multiple device_id are not allowed", conf.path, line_no);
					exit(1);
				}

				if (strlen(val) > 31) {
					printf("ERROR: %s[%d]: device_id is too long (max 31 chars)", conf.path, line_no);
					exit(1);
				}

				device_id = strdup(val);
				continue;
			}

			// got a reasonably valid line
			if (!strcmp(name, "no")) {
				if (conf_started)
					complete_conf(&conf);

				conf = conf_tpl;
				conf.index = atoi(val);
				if (conf.index < 0 || conf.index >= 32) {
					printf("ERROR: %s[%d]: no out of range (is %d, must be 0-32)", conf.path, line_no, conf.index);
					exit(1);
				}

				conf_started = true;
				continue;
			}

			if (!conf_started) {
				printf("ERROR: %s[%d]: Config has to start with no=[idx]", conf.path, line_no);
				exit(1);
			}

			if (!strcmp(name, "name"))
				snprintf(conf.name, sizeof conf.name, "%s", val);

			if (!strcmp(name, "bootargs"))
				snprintf(conf.bootargs, sizeof conf.bootargs, "%s", val);

			for (int i = 0; i < sizeof(image_types) / sizeof(image_types[0]); i++) {
				if (strcmp(name, image_types[i].conf_var))
					continue;

				char path[PATH_MAX];
				snprintf(path, sizeof path, "%s/%s", conf_dir, val);
				struct data* d = data_add_file(path);

				struct bconf_image* im = malloc(sizeof *im), *imi, *imi_last;
				assert(im != NULL);
				memset(im, 0, sizeof *im);

				for (imi = conf.images, imi_last = imi; imi; imi_last = imi, imi = imi->next) {
					if (imi->type == image_types[i].type) {
						printf("ERROR: %s[%d]: Image '%s' is already set for no=%d", conf.path, line_no, image_types[i].conf_var, conf.index);
						exit(1);
					}
				}

				im->type = image_types[i].type;
				im->data = d;

				if (imi_last)
					imi_last->next = im;
				else
					conf.images = im;
			}
		}
	}

	if (conf_started)
		complete_conf(&conf);

	fclose(f);
	return true;
}

static void include_files(const char* dir)
{
	DIR* d = opendir(dir);
	if (!d) {
		printf("ERROR: Can't open directory '%s'\n", dir);
		exit(1);
	}

	struct dirent *e;
	while (1) {
		errno = 0;
		e = readdir(d);
		if (!e) {
			if (errno) {
				printf("ERROR: failed to read directory '%s'\n", dir);
				exit(1);
			}

			break;
		}

		char path[PATH_MAX];
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		struct stat st;
		int ret = stat(path, &st);
		if (ret) {
			printf("ERROR: failed to stat %s\n", path);
			exit(1);
		}

		if (S_ISREG(st.st_mode)) {
			if (strlen(e->d_name) > 31) {
				printf("ERROR: File name too long '%s', max is 31 chars\n", path);
				exit(1);
			}

			if (n_files >= sizeof(files) / sizeof(files[0])) {
				printf("ERROR: Too many files\n", path);
				exit(1);
			}

			struct data* d = data_add_file(path);
			struct file* f = &files[n_files++];

			f->data = d;
			snprintf(f->name, sizeof f->name, "%s", e->d_name);
		}
	}

	closedir(d);
}

// }}}
// {{{ Write filesystem

void write_checked(int fd, void* buf, size_t len)
{
	ssize_t ret = write(fd, buf, len);
	if (ret < 0 || ret != len) {
		printf("ERROR: failed writing bootfs!!! %s\n", strerror(errno));
		exit(1);
	}
}

off_t lseek_checked(int fd, off_t off)
{
	off_t off_out = lseek(fd, off, SEEK_SET);
	if (off_out < 0) {
		printf("ERROR: failed writing bootfs!!! %s\n", strerror(errno));
		exit(1);
	}

	return off_out;
}

size_t write_fd_checked(int dest_fd, int src_fd)
{
	size_t len = 0;

	while (1) {
		char buf[1024*128];

		ssize_t read_b = read(src_fd, buf, sizeof buf);
		if (read_b < 0) {
			printf("ERROR: failed writing bootfs!!! %s\n", strerror(errno));
			exit(1);
		} else if (read_b == 0) {
			break;
		}

		ssize_t wr_b = write(dest_fd, buf, read_b);
		if (read_b != wr_b) {
			printf("ERROR: failed writing bootfs!!! %s\n", strerror(errno));
			exit(1);
		}

		len += wr_b;
	}

	return len;
}

static void usage(const char* msg)
{
	printf("ERROR: %s\n", msg);
	printf("Usage: p-boot-conf <conf-dir> <blk-dev>\n\n");
	printf("Example: p-boot-conf /boot /dev/mmclbk1p1\n");
	exit(1);
}

int main(int ac, char* av[])
{
	const char* conf_dir;
	const char* blk_dev;

	if (ac != 3)
		usage("mising options");

	conf_dir = av[1];
	blk_dev = av[2];

	parse_conf(conf_dir, "boot.conf");

	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/files", conf_dir);
	include_files(path);

	/* open bootfs partition block device */
	int fd = open(blk_dev, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		perror("can't open block device\n");
		exit(1);
	}

	lseek_checked(fd, 0);

	struct bootfs_sb sb = {
		.magic = ":BOOTFS:",
		.version = htobe32(1),
	};
	
	if (device_id)
		snprintf(sb.device_id, sizeof sb.device_id, "%s", device_id);

	write_checked(fd, &sb, sizeof sb);

	off_t off_c = 2048;
	off_t off_i = 2048 * 33;
	int files_written = 0;

        // write data images
	printf("Data space:\n\n");
	for (struct data* d = data_list; d; d = d->next) {
		lseek_checked(fd, off_i);
		lseek_checked(d->fd, 0);

		d->offset = off_i;
		d->size = write_fd_checked(fd, d->fd);

		off_i += d->size;
		if (off_i % 512)
			off_i += (512 - off_i % 512);

		printf("    %08x-%08x: %s (size %u KiB)\n", d->offset, d->offset + d->size, d->path, d->size / 1024);
	}

	printf("\nBoot configurations:\n\n");
	for (int i = 0; i < 32; i++) {
		if (confs[i].used) {
			struct bootfs_conf bc = {
				.magic = ":BFCONF:",
			};

			printf("no=%d (%s)\n\n", confs[i].index, confs[i].name);
			printf("  %s\n\n", confs[i].bootargs);

			snprintf(bc.name, sizeof bc.name, "%s", confs[i].name);
			snprintf(bc.boot_args, sizeof bc.boot_args, "%s", confs[i].bootargs);

			int n_imgs = 0;
			for (struct bconf_image* im = confs[i].images; im; im = im->next) {
				bc.images[n_imgs].type = htobe32(im->type);
				bc.images[n_imgs].data_off = htobe32(im->data->offset);
				bc.images[n_imgs++].data_len = htobe32(im->data->size);

				printf("  %c %08x-%08x %s\n", im->type, im->data->offset, im->data->offset + im->data->size, im->data->path);
			}

			lseek_checked(fd, off_c);
			write_checked(fd, &bc, sizeof bc);

			printf("\n");
		} else if (files_written < n_files) {
			printf("file list block %d\n\n", i);

			struct bootfs_files bf = {
				.magic = ":BFILES:",
			};

			int files_rem = n_files - files_written;
			files_rem = files_rem > 51 ? 51 : files_rem;

			for (int i = 0; i < files_rem; i++) {
				struct file* f = &files[files_written];

				snprintf(bf.files[i].name, sizeof bf.files[i].name, "%s", f->name);
				bf.files[i].data_off = htobe32(f->data->offset);
				bf.files[i].data_len = htobe32(f->data->size);

				printf("  %08x-%08x %s %s\n",
				       f->data->offset, f->data->offset + f->data->size,
				       f->name, f->data->path);

				files_written++;
			}

			lseek_checked(fd, off_c);
			write_checked(fd, &bf, sizeof bf);

			printf("\n");
		} else {
			char zero[2048] = {0};
			lseek_checked(fd, off_c);
			write_checked(fd, &zero, sizeof zero);
		}

		off_c += 2048;
	}

	printf("Total filesystem size %zu KiB\n\n", off_i / 1024);

	close(fd);
	sync();
	return 0;
}
