/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#define KERNEL_MAX_END_PADDR (KERNEL_PADDR + KERNEL_MAX_SIZE)

/*
 * Static address of a single bios memory area struct: it must be in the lowest
 * 64 KB and must be hard-coded (because we don't know the memory map yet!).
 */
#define BIOS_MEM_AREA_BUF   (16 * KB)

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
