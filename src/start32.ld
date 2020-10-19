ENTRY(entry)

MEMORY
{
	aa32_code  : o = 0x10068,    l = 0x7c
	aa32_entry : o = 0x100e4,    l = 0x80
}

SECTIONS
{
	.entry : {
		*(.entry*)
	} >aa32_entry

	.text : {
		*(.text*)
		. = ALIGN(4);
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
		. = ALIGN(4);
		*(.data*)
		. = ALIGN(4);
		*(.bss*)
	} >aa32_code

	/DISCARD/ : {
		*(*)
	}
}
