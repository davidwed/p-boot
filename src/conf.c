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

void usage(const char* msg)
{
	printf("ERROR: %s\n", msg);
	printf("Usage: mkbootfs [conf-dir] [blk-dev]\n");
	exit(1);
}

struct bconf {
	int used;
	int index;
	char path[1024];
	char name[1024];

	// image file fds
	int linuximg;
	int initramfs;
	int atf;
	int dtb;
	char bootargs[4096];

	char linuximg_path[PATH_MAX];
	char initramfs_path[PATH_MAX];
	char atf_path[PATH_MAX];
	char dtb_path[PATH_MAX];
};

static struct bconf confs[32];

bool str_ends_with(const char* s, const char* ext)
{
	size_t len = strlen(s);
	size_t ext_len = strlen(ext);

	return !strcmp(s + (len - ext_len), ext);
}

void complete_conf(struct bconf* c)
{
	if (confs[c->index].used) {
		printf("ERROR: %s: Configuration slot no=%d is already taken by '%s'\n", c->path, c->index, confs[c->index].path);
		exit(1);
	}

	if (c->atf < 0) {
		printf("ERROR: %s: Configuration slot no=%d is missing 'atf' image\n", c->path, c->index);
		exit(1);
	}

	if (c->linuximg < 0) {
		printf("ERROR: %s: Configuration slot no=%d is missing 'linux' image\n", c->path, c->index);
		exit(1);
	}

	if (c->dtb < 0) {
		printf("ERROR: %s: Configuration slot no=%d is missing 'dtb' image\n", c->path, c->index);
		exit(1);
	}

	c->used = 1;
	confs[c->index] = *c;
}

bool parse_conf(const char* conf_dir, const char* conf_filename)
{
	struct bconf conf_tpl = {
		.linuximg = -1,
		.atf = -1,
		.dtb = -1,
		.initramfs = -1,
	};

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

			if (!strcmp(name, "linux") || !strcmp(name, "initramfs") || !strcmp(name, "atf") || !strcmp(name, "dtb")) {
				char path[1024];
				snprintf(path, sizeof path, "%s/%s", conf_dir, val);

				int fd = open(path, O_RDONLY);
				if (fd < 0) {
					printf("ERROR: %s[%d]: Can't open image file '%s' for '%s'", conf.path, line_no, val, name);
					exit(1);
				}

				if (!strcmp(name, "linux")) {
					conf.linuximg = fd;
					realpath(path, conf.linuximg_path);
				} else if (!strcmp(name, "initramfs")) {
					conf.initramfs = fd;
					realpath(path, conf.initramfs_path);
				} else if (!strcmp(name, "atf")) {
					conf.atf = fd;
					realpath(path, conf.atf_path);
				} else if (!strcmp(name, "dtb")) {
					conf.dtb = fd;
					realpath(path, conf.dtb_path);
				}
			}

			if (!strcmp(name, "bootargs"))
				snprintf(conf.bootargs, sizeof conf.bootargs, "%s", val);

			if (!strcmp(name, "name"))
				snprintf(conf.name, sizeof conf.name, "%s", val);
		}
	}

	if (conf_started)
		complete_conf(&conf);

	fclose(f);
	return true;
}

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

int main(int ac, char* av[])
{
	const char* conf_dir;
	const char* blk_dev;

	if (ac != 3)
		usage("mising option");

	conf_dir = av[1];
	blk_dev = av[2];

	/* read *.conf files from config directory into confs[] */
	DIR* d = opendir(conf_dir);
	if (!d)
		usage("conf directory is not a directory");

	struct dirent *e;
	while (1) {
		errno = 0;
		e = readdir(d);
		if (!e) {
			if (errno) {
				printf("ERROR: failed to read directory\n");
				exit(1);
			}

			break;
		}

		if (str_ends_with(e->d_name, ".conf"))
			parse_conf(conf_dir, e->d_name);
	}

	closedir(d);

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

	write_checked(fd, &sb, sizeof sb);

	off_t off_c = 2048;
	off_t off_i = 2048 * 33;
	for (int i = 0; i < 32; i++) {
		if (confs[i].used) {
			int n_imgs = 0;
			struct bootfs_conf bc = {
				.magic = ":BFCONF:",
			};

			printf("Writing boot configuration %d (%s)\n", confs[i].index, confs[i].path);
			printf("  %-8s %s\n", "Name:", confs[i].name);

#define WRITE_IMG(n, t, desc) \
			if (confs[i].n >= 0) { \
				bc.images[n_imgs].type = htobe32(t); \
				bc.images[n_imgs].data_off = htobe32(off_i); \
				lseek_checked(fd, off_i); \
				lseek_checked(confs[i].n, 0); \
				size_t len = write_fd_checked(fd, confs[i].n); \
				bc.images[n_imgs++].data_len = htobe32(len); \
				off_i += len; \
				if (off_i % 512) \
					off_i += (512 - off_i % 512); \
				printf("  %-8s %s (at %08x, size %zu)\n", desc, confs[i].n##_path, off_i, len); \
			}

			// build images list
			WRITE_IMG(atf, 'A', "ATF:")
			WRITE_IMG(dtb, 'D', "DTB:")
			WRITE_IMG(linuximg, 'L', "Linux:")
			WRITE_IMG(initramfs, 'I', "Initrd:")

			snprintf(bc.boot_args, sizeof bc.boot_args, "%s", confs[i].bootargs);
			snprintf(bc.name, sizeof bc.name, "%s", confs[i].name);

			lseek_checked(fd, off_c);
			write_checked(fd, &bc, sizeof bc);

			printf("\n");
		} else {
			char zero[2048] = {0};
			lseek_checked(fd, off_c);
			write_checked(fd, &zero, sizeof zero);
		}

		off_c += 2048;
	}

	close(fd);
}
