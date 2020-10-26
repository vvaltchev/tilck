/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/boot/common.h>
#include <multiboot.h>

/*
 * Static address of a single bios memory area struct: it must be in the lowest
 * 64 KB and must be hard-coded (because we don't know the memory map yet!).
 */
#define BIOS_MEM_AREA_BUF   (16 * KB)

struct mem_info;

extern const struct bootloader_intf legacy_boot_intf;
extern struct ModeInfoBlock *usable_vbe_mode_info_block;
extern struct VbeInfoBlock *vbe_info_block;
extern video_mode_t selected_mode;
extern struct mem_info g_meminfo;
extern ulong bp_paddr;
extern u32 bp_size;
extern void *loaded_kernel_file;

multiboot_info_t *
setup_multiboot_info(ulong ramdisk_paddr, ulong ramdisk_size);

u32
do_ramdisk_compact_clusters(void *ramdisk, u32 rd_size);

void
load_fat_ramdisk(const char *load_str,
                 u32 first_sec,
                 ulong min_paddr,
                 ulong *ref_rd_paddr,
                 u32 *ref_rd_size,
                 bool alloc_extra_page);

bool
load_kernel_file(ulong ramdisk,
                 ulong ramdisk_size,
                 const char *filepath,
                 void **file_paddr);

