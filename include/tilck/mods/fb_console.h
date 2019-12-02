/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <multiboot.h>

extern bool __use_framebuffer;

static ALWAYS_INLINE bool use_framebuffer(void)
{
   return __use_framebuffer;
}

void set_framebuffer_info_from_mbi(multiboot_info_t *mbi);
void init_framebuffer_console(void);
void debug_dump_glyph(u32 n);

void init_fbdev(void);
void fb_fill_fix_info(void *fix_info);
void fb_fill_var_info(void *var_info);
void fb_user_mmap(void *vaddr, size_t mmap_len);

// Runtime debug info funcs

bool fb_is_using_opt_funcs(void);
u32 fb_get_res_x(void);
u32 fb_get_res_y(void);
u32 fb_get_bbp(void);
u32 fb_get_font_w(void);
u32 fb_get_font_h(void);
