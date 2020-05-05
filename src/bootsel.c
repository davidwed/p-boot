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

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <endian.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

static bool set_rtc_data_reg(uint32_t reg, uint32_t val)
{
	long page_size = -1;
	int fd = -1;

	// RTC data regs
	off_t off = 0x01f00100;
	size_t len = 0x100;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 0)
		return false;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0)
		return false;

	off_t o = off - off % page_size;
	size_t s = len + off % page_size;
	s = s + ((s % page_size > 0) ? page_size - s % page_size : 0);

	void* base = mmap(0, s, PROT_READ | PROT_WRITE, MAP_SHARED, fd, o);
	close(fd);

	if (base == MAP_FAILED)
		return false;

	volatile uint32_t *rtc_data = base + off % page_size;
	
	rtc_data[reg] = val;
	
	munmap(base, s);
	return true;
}

static bool set_bootconf_default(const char* path, uint32_t val)
{
	int fd = open(path, O_RDWR);
	if (fd < 0)
		return false;

	if (lseek(fd, 8 + 4, SEEK_SET) < 0) {
		close(fd);
		return false;
	}

	val = htobe32(val);

	if (write(fd, &val, sizeof val) != 4) {
		close(fd);
		return false;
	}

	close(fd);
	return true;
}

int main(int ac, char* av[])
{
	if (ac == 3 || ac == 4) {
		uint32_t val = atoi(av[2]);
		if (val > 32) {
			printf("ERROR: Invalid boot configuration '%s', use values 0-32\n", av[2]);
			return 1;
		}
		
		if (ac == 4) {
			if (strcmp(av[3], "once")) {
				printf("ERROR: Invalid boot configuration validity '%s', use 'once' or nothing\n", av[3]);
				return 1;
			}
			
			if (strcmp(av[1], "rtc")) {
				printf("ERROR: Only 'rtc' supports 'once'\n");
				return 1;
			}

			val |= 0x80;
		}

		if (!strcmp(av[1], "rtc")) {
			if (!set_rtc_data_reg(0, val + 1)) {
				printf("ERROR: Can't update boot configuration selection via RTC data register\n");
				return 1;
			}
				
		} else {
			if (!set_bootconf_default(av[1], val)) {
				printf("ERROR: Can't update boot configuration selection via BOOTFS at %s\n", av[1]);
				return 1;
			}
		}
		
		return 0;
	}

	printf("Usage: %s /dev/mmcblk2p1 2\n", av[0]);
	printf("    or %s rtc 3\n", av[0]);
	printf("    or %s rtc 0 once\n", av[0]);
	return 1;
}
