/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#define RAMDISK_PADDR   (KERNEL_PADDR + KERNEL_MAX_SIZE)
#define MBI_PADDR       (64 * KB)
#define MEM_AREAS_BUF   (16 * KB)

/*
 * Checks if 'addr' is in the range [begin, end).
 */
#define IN(addr, begin, end) ((begin) <= (addr) && (addr) < (end))

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
