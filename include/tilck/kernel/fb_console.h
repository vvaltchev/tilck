
#pragma once

#include <tilck/common/basic_defs.h>
#include <multiboot.h>

#define fb_make_color(r, g, b) (((r) << 16) | ((g) << 8) | (b))

extern bool __use_framebuffer;

static ALWAYS_INLINE bool use_framebuffer(void)
{
   return __use_framebuffer;
}

void set_framebuffer_info_from_mbi(multiboot_info_t *mbi);
void init_framebuffer_console(bool use_also_serial_port);
void debug_dump_glyph(u32 n);
