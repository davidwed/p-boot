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

bool parse_conf(struct bconf* c, const char* conf_dir)
{
	FILE* f = fopen(c->path, "r");
	if (!f) {
		printf("ERROR: can't open %s", c->path);
		exit(1);
	}

	char line[4096];

	while (fgets(line, sizeof line, f)) {
		int len = strlen(line);
		if (line[len - 1] == '\n')
			line[len - 1] = 0;

//		printf("parsing: %s %s\n", c->path, line);

		char* val = strchr(line, '=');
		if (val) {
			*val++ = 0;

			// got a reasonably valid line
			if (!strcmp(line, "no")) {
				c->index = atoi(val);
				if (c->index < 0 || c->index >= 32) {
					printf("ERROR: invalid no=%d in %s\n", c->index, c->path);
					exit(1);
				}
			}

			if (!strcmp(line, "linux") || !strcmp(line, "initramfs") || !strcmp(line, "atf") || !strcmp(line, "dtb")) {
				char path[1024];
				snprintf(path, sizeof path, "%s/%s", conf_dir, val);

				int fd = open(path, O_RDONLY);
				if (fd < 0) {
					printf("ERROR: can't open %s=%s in %s\n", line, val, c->path);
					exit(1);
				}

				if (!strcmp(line, "linux")) {
					c->linuximg = fd;
					realpath(path, c->linuximg_path);
				} else if (!strcmp(line, "initramfs")) {
					c->initramfs = fd;
					realpath(path, c->initramfs_path);
				} else if (!strcmp(line, "atf")) {
					c->atf = fd;
					realpath(path, c->atf_path);
				} else if (!strcmp(line, "dtb")) {
					c->dtb = fd;
					realpath(path, c->dtb_path);
				}
			}

			if (!strcmp(line, "bootargs")) {
				snprintf(c->bootargs, sizeof c->bootargs, "%s", val);
			}
		}
	}

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

	DIR* d = opendir(conf_dir);
	if (!d)
		usage("conf directory is not a directory");

	char path[1024];
	snprintf(path, sizeof path, "%s/%s", conf_dir, "bl31.bin");
	int atf_fd = open(path, O_RDONLY);

	for (int i = 0; i < 32; i++) {
		confs[i].linuximg = -1;
		confs[i].atf = -1;
		confs[i].dtb = -1;
		confs[i].initramfs = -1;
	}

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

		if (str_ends_with(e->d_name, ".conf")) {
			struct bconf conf = {
				.linuximg = -1,
				.atf = atf_fd,
				.dtb = -1,
				.initramfs = -1,
			};
			snprintf(conf.path, sizeof conf.path, "%s/%s", conf_dir, e->d_name);

			if (atf_fd >= 0)
				realpath(path, conf.atf_path);

			parse_conf(&conf, conf_dir);

			if (confs[conf.index].used) {
				printf("ERROR: configuration slot %d is already taken by %s\n", conf.index, confs[conf.index].path);
				exit(1);
			}

			conf.used = 1;
			confs[conf.index] = conf;
		}
	}

	closedir(d);

	int fd = open(blk_dev, O_RDWR/* | O_CREAT | O_TRUNC*/, 0666);
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
