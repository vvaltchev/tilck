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

struct fb_console_info {

   u16 res_x;
   u16 res_y;
   u16 bpp;
   u16 font_h;
   u16 font_w;
};

bool fb_is_using_opt_funcs(void);
void fb_console_get_info(struct fb_console_info *i);
