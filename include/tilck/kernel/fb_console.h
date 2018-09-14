/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <multiboot.h>

extern bool __use_framebuffer;

extern u8 fb_red_pos;
extern u8 fb_green_pos;
extern u8 fb_blue_pos;
extern u8 fb_red_mask_size;
extern u8 fb_green_mask_size;
extern u8 fb_blue_mask_size;
extern u32 fb_red_mask;
extern u32 fb_green_mask;
extern u32 fb_blue_mask;

static ALWAYS_INLINE bool use_framebuffer(void)
{
   return __use_framebuffer;
}

inline u32 fb_make_color(u32 r, u32 g, u32 b)
{
   return ((r << fb_red_pos) & fb_red_mask) |
          ((g << fb_green_pos) & fb_green_mask) |
          ((b << fb_blue_pos) & fb_blue_mask);
}

void set_framebuffer_info_from_mbi(multiboot_info_t *mbi);
void init_framebuffer_console(bool use_also_serial_port);
void debug_dump_glyph(u32 n);

void init_fbdev(void);
void fb_fill_fix_info(void *fix_info);
void fb_fill_var_info(void *var_info);
void fb_user_mmap(void *vaddr, size_t mmap_len);
