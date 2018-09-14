/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#define PSF2_FONT_MAGIC 0x864ab572

typedef struct {
    u32 magic;
    u32 version;          /* zero */
    u32 header_size;
    u32 flags;            /* 0 if there's no unicode table */
    u32 glyphs_count;
    u32 bytes_per_glyph;
    u32 height;          /* height in pixels */
    u32 width;           /* width in pixels */
} psf2_header;

extern psf2_header *fb_font_header;
extern u32 vga_rgb_colors[16];

u32 fb_get_width(void);
u32 fb_get_height(void);
u32 fb_get_bpp(void);

void fb_map_in_kernel_space(void);
void fb_raw_color_lines(u32 iy, u32 h, u32 color);
void fb_draw_cursor_raw(u32 ix, u32 iy, u32 color);
void fb_draw_char_failsafe(u32 x, u32 y, u16 entry);
void fb_draw_char_optimized(u32 x, u32 y, u16 e);
void fb_draw_char_optimized_row(u32 y, u16 *entries, u32 count);
void fb_copy_from_screen(u32 ix, u32 iy, u32 w, u32 h, u32 *buf);
void fb_copy_to_screen(u32 ix, u32 iy, u32 w, u32 h, u32 *buf);
void fb_lines_shift_up(u32 src_y, u32 dst_y, u32 lines_count);
bool fb_pre_render_char_scanlines(void);
bool fb_alloc_shadow_buffer(void);
void fb_raw_perf_screen_redraw(u32 color, bool use_fpu);
