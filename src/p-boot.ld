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

OUTPUT_FORMAT("elf64-littleaarch64", "elf64-littleaarch64", "elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

MEMORY
{
	sram : o = 0x10060,    l = 0x7fa0
	dram : o = 0x40000000, l = 2048M
}

SECTIONS
{
	.text : {
		. = ALIGN(8);
		*start.o (.text*)

		/* We put code/data necessary for jump to Linux in the first 1KiB,
		 * because 1KiB+ is clobbered by ATF NOBITS. */
		*(.lowtext*)
		*(.lowdata*)

		. = ALIGN(8);
		*(.text*)
	} >sram

	.data : {
		. = ALIGN(8);
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
		*(.data*)
	} >sram

	.bss : {
		. = ALIGN(8);
		*(.bss*)
	} >sram

	/DISCARD/ : {
		*(*)
	}
}
