
#pragma once

/*
 * VGA text mode color constants.
 *
 * These colors and macros however are also used for the fb_console: afterall,
 * it is convenient for any console implementation to use the same set of colors
 * and the format of entry (char + color). This will apply (very likely) even
 * for non-x86 architectures like ARM.
 */
enum vga_color {
   COLOR_BLACK = 0,
   COLOR_BLUE = 1,
   COLOR_GREEN = 2,
   COLOR_CYAN = 3,
   COLOR_RED = 4,
   COLOR_MAGENTA = 5,
   COLOR_BROWN = 6,
   COLOR_LIGHT_GREY = 7,

   COLOR_DARK_GREY = 8,
   COLOR_LIGHT_BLUE = 9,
   COLOR_LIGHT_GREEN = 10,
   COLOR_LIGHT_CYAN = 11,
   COLOR_LIGHT_RED = 12,
   COLOR_LIGHT_MAGENTA = 13,
   COLOR_LIGHT_BROWN = 14,
   COLOR_WHITE = 15,
};

#define make_color(fg, bg) ((fg) | (bg) << 4)
#define make_vgaentry(c, color) (((u16)c) | ((u16)color << 8))

#define vgaentry_char(e) ((e) & 0xFF)
#define vgaentry_color(e) ((e) >> 8)

#define vgaentry_color_fg(color) ((color) & 0xF)
#define vgaentry_color_bg(color) (((color) >> 4) & 0xF)

#define vgaentry_fg(e) (((e) >> 8) & 0xF)
#define vgaentry_bg(e) (((e) >> 12) & 0xF)
