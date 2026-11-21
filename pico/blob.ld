/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 TheGammaSqueeze */
/* armSNES-pico: link script for the loadable SNES core blob (see snes_core_abi.h).
 *
 * The LK copies the flat image to a fixed DRAM address, zeroes [load_size, total_span), syncs the
 * caches and calls the entry found through the header. armSNES takes the SNES slot and the Genesis
 * and NES cores' slot after it, 0x4F000000 up to the shared emulator arena at 0x50000000 (16 MiB):
 * its picture buffers and memory maps are static, and only one core runs at a time.
 * Everything that depends on the game (the cartridge, its RAM, the chips' memories) is
 * allocated from the arena the LK hands to heap_init, not placed here.
 *
 * Header (image offset 0):
 *   u32 magic      0x31534E53 "SNS1"
 *   u32 version    6 (SNES_CORE_ABI_V6: change both together, the link script cannot include
 *                     the header)
 *   u32 entry      offset of snes_core_blob_init
 *   u32 load_size  bytes present in the file
 *   u32 total_span bytes to reserve, BSS included
 */
ENTRY(snes_core_blob_init)

BLOB_VMA = 0x4F000000;
BLOB_END = 0x50000000;

SECTIONS
{
	. = BLOB_VMA;
	.header : {
		LONG(0x31534E53);
		LONG(6);
		LONG(snes_core_blob_init - BLOB_VMA);
		LONG(__blob_load_end - BLOB_VMA);
		LONG(__blob_end - BLOB_VMA);
	}
	.text : { KEEP(*(.text.snes_core_blob_init)) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	.init_array : { __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .; }
	/* the padding to 16 bytes sits inside .data, the last section in the file, so the flat image
	 * always carries it and its size is exactly the load size in the header */
	.data : { *(.data .data.*) . = ALIGN(16); }
	__blob_load_end = .;
	.bss (NOLOAD) : {
		__bss_start__ = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(16);
		__bss_end__ = .;
	}
	__blob_end = .;
	end = .;
	/DISCARD/ : { *(.comment) *(.ARM.attributes) *(.note.*) *(.ARM.exidx*) *(.ARM.extab*) }
}
ASSERT(__blob_end <= BLOB_END, "armSNES-pico: the blob runs into the emulator arena at 0x50000000")
