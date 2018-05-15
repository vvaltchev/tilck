
#pragma once

#include <common/basic_defs.h>
#include <multiboot.h>

extern bool use_framebuffer;

#define fb_make_color(r, g, b) (((r) << 16) | ((g) << 8) | (b))


void set_framebuffer_info_from_mbi(multiboot_info_t *mbi);
void init_framebuffer_console(void);
