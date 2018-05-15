
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

extern char _binary_font_psf_start;

u32 fb_get_width(void);
u32 fb_get_height(void);

void fb_map_in_kernel_space(void);
void fb_raw_bzero_lines(u32 iy, u32 h);
void fb_draw_cursor_raw(u32 ix, u32 iy, u32 color);
void fb_draw_char_raw(u32 x, u32 y, u32 fg, u32 bg, u32 c);
void fb_copy_from_screen(u32 ix, u32 iy, u32 w, u32 h, u32 *buf);
void fb_copy_to_screen(u32 ix, u32 iy, u32 w, u32 h, u32 *buf);
