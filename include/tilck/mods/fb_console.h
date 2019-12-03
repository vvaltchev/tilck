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
void init_fb_console(void);

// Debug/devel function
void debug_dump_glyph(u32 n);

// Runtime debug info funcs [debugpanel]
bool fb_is_using_opt_funcs(void);
u32 fb_get_res_x(void);
u32 fb_get_res_y(void);
u32 fb_get_bbp(void);
u32 fb_get_font_w(void);
u32 fb_get_font_h(void);
