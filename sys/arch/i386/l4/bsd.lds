/*
 * Copyright (c) 2010 Christian Ludwig <cludwig@net.t-labs.tu-berlin.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Linker script for the final bsd executable. Shamelessly borrowed from the
 * amd64 architecture.
 */

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)

/* Define how we want out ELF binary to look like. */
PHDRS
{
	text PT_LOAD FILEHDR PHDRS;
	rodata PT_LOAD;
	data PT_LOAD;
	bss PT_LOAD;
}

/*
 * If we want the text/rodata/data sections aligned on 2M boundaries,
 * we could use the following instead.  Note, file size would increase
 * due to necessary padding.
 *
 *__ALIGN_SIZE = 0x200000;
 */
__ALIGN_SIZE = 0x1000;
__kernel_base_virt = 0x400000 + SIZEOF_HEADERS;
__kernel_base_phys = __kernel_base_virt;

/* We use physical address to jump to kernel */
start_phys = LOADADDR(.text) + (__kernel_base_virt);
ENTRY(_start)	/* defined in head.S */
SECTIONS
{
	__kernel_text_virt = __kernel_base_virt;
	__kernel_text_phys = __kernel_base_phys;
	.text (__kernel_text_virt) : AT (__kernel_text_phys)
	{
		__text_size = SIZEOF(.text);
		__text_load = LOADADDR(.text);
		*(.text)
	} :text
	PROVIDE (__etext = .);
	PROVIDE (etext = .);
	_etext = .;

        /* Move rodata to the next page, so we can nuke X and W bit on them */
	. = ALIGN(__ALIGN_SIZE);
	__kernel_rodata_virt = .;
	__kernel_rodata_phys = .;
	.rodata (__kernel_rodata_virt) : AT (__kernel_rodata_phys)
	{
		__rodata_start = ABSOLUTE(.);
		__rodata_size = SIZEOF(.rodata);
		__rodata_load = LOADADDR(.rodata);
		*(.rodata)
	} :rodata
	PROVIDE (erodata = .);
	_erodata = .;

	/* Move data to the next page, so we can add W bit on them */
	. = ALIGN(__ALIGN_SIZE);
	__kernel_data_virt = .;
	__kernel_data_phys = .;
	.data (__kernel_data_virt) : AT (__kernel_data_phys)
	{
		__data_start = ABSOLUTE(.);
		__data_size = SIZEOF(.data);
		__data_load = LOADADDR(.data);
		*(.data)

		. = ALIGN(4);
		__l4env_initcall_start = .;
		*(.l4env_initcall.init);
		__l4env_initcall_end = .;
	} :data
	. = ALIGN(0x1000);
	PROVIDE (edata = . );
	_edata = .;

        /* BSS starts right after padded data */
	__kernel_bss_virt = .;
	__kernel_bss_phys = .;
	.bss (__kernel_bss_virt) : AT (__kernel_bss_phys)
	{
		__bss_start = ABSOLUTE(.);
		__bss_size = SIZEOF(.bss);
		__bss_load = LOADADDR(.bss);
		*(.bss)
		*(COMMON)
		/* Align here to ensure that the .bss section occupies space
		 * up to _end.  Align after .bss to ensure correct alignment
		 * even if the .bss section disappears because there are no
		 * input sections.
		 */
		. = ALIGN(64 / 8);
	} :bss
	. = ALIGN(64 / 8);
	_end = .;
	PROVIDE (end = .);

	/* XXX - hack alert, since we are not C++, nuke these */
	/DISCARD/ :
	{
		*(.comment)
		*(.note.GNU-stack)
		*(.eh_frame)
	}
}
