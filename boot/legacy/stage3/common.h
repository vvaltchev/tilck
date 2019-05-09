/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#define KERNEL_MAX_END_PADDR (KERNEL_PADDR + KERNEL_MAX_SIZE)

/* Where the memory map will be: must be in the lowest 64 KB and hard-coded. */
#define BIOS_MEM_AREA_BUF   (15 * KB)
#define MEM_AREAS_BUF       (16 * KB)
#define MEM_AREAS_BUF_SIZE  (48 * KB)

/* Where the multiboot info will be stored. In theory, it could be anywhere. */
#define MBI_PADDR           (64 * KB)

extern u32 fb_paddr;
extern u32 fb_pitch;
extern u32 fb_width;
extern u32 fb_height;
extern u32 fb_bpp;

extern u8 fb_red_pos;
extern u8 fb_red_mask_size;
extern u8 fb_green_pos;
extern u8 fb_green_mask_size;
extern u8 fb_blue_pos;
extern u8 fb_blue_mask_size;

extern bool graphics_mode;
extern u16 selected_mode;
