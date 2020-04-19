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
    sram    (rwx) : ORIGIN = 0x10060,    LENGTH = 0x7fa0
    sdram   (rwx) : ORIGIN = 0x4ff80000, LENGTH = 0x80000
}

SECTIONS
{
	.text : {
		. = ALIGN(8);
		*start.o (.text)

		/* We put code/data necessary for jump to Linux in the first 1KiB,
		 * because 1KiB+ is clobbered by ATF NOBITS. */
		*(.textlow)
		*(.lowdata)

		. = ALIGN(8);
		*(.text*)
	} >sram

	.rodata . : {
		. = ALIGN(8);
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
	} >sram

	.data . : {
		. = ALIGN(8);
		*(.data*)
	} >sram

	.bss . : {
		. = ALIGN(8);
		*(.bss*)
	} >sram

	/DISCARD/ : { *(.comment*) }
	/DISCARD/ : { *(.dynstr*) }
	/DISCARD/ : { *(.dynamic*) }
	/DISCARD/ : { *(.plt*) }
	/DISCARD/ : { *(.interp*) }
	/DISCARD/ : { *(.gnu*) }
	/DISCARD/ : { *(.note*) }
	/DISCARD/ : { *(.eh_frame*) }
}
