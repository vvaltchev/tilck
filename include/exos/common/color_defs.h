
#pragma once

/*
 * exOS text mode color constants
 */
enum vga_color {

   /* regular (darker) colors */
   COLOR_BLACK = 0,
   COLOR_BLUE = 1,
   COLOR_GREEN = 2,
   COLOR_CYAN = 3,
   COLOR_RED = 4,
   COLOR_MAGENTA = 5,
   COLOR_YELLOW = 6,         /* on VGA this is kind-of brown */
   COLOR_WHITE = 7,          /* light gray */

   /* brighter colors */
   COLOR_BRIGHT_BLACK = 8,   /* dark gray */
   COLOR_BRIGHT_BLUE = 9,
   COLOR_BRIGHT_GREEN = 10,
   COLOR_BRIGHT_CYAN = 11,
   COLOR_BRIGHT_RED = 12,
   COLOR_BRIGHT_MAGENTA = 13,
   COLOR_BRIGHT_YELLOW = 14,
   COLOR_BRIGHT_WHITE = 15,
};

#define make_color(fg, bg) ((fg) | (bg) << 4)
#define get_color_fg(color) ((color) & 0xF)
#define get_color_bg(color) (((color) >> 4) & 0xF)

#define DEFAULT_FG_COLOR COLOR_BRIGHT_WHITE
#define DEFAULT_BG_COLOR COLOR_BLACK

/*
 * Entry defs (color + char): the hardware format (VGA textmode) is used also
 * by the hw-independents "term" and "fb_console" for convenience.
 */
#define make_vgaentry(c, color) (((u16)c) | ((u16)color << 8))
#define vgaentry_get_fg(e) (((e) >> 8) & 0xF)
#define vgaentry_get_bg(e) (((e) >> 12) & 0xF)
#define vgaentry_get_char(e) ((e) & 0xFF)
#define vgaentry_get_color(e) ((e) >> 8)
