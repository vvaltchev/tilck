/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

/*
 * Tilck text mode color constants
 */
enum vga_color {

   /* regular (darker) colors */
   COLOR_BLACK          =  0,
   COLOR_BLUE           =  1,
   COLOR_GREEN          =  2,
   COLOR_CYAN           =  3,
   COLOR_RED            =  4,
   COLOR_MAGENTA        =  5,
   COLOR_YELLOW         =  6,     /* on VGA this is kind-of brown */
   COLOR_WHITE          =  7,     /* light gray */

   /* brighter colors */
   COLOR_BRIGHT_BLACK   =  8,    /* dark gray */
   COLOR_BRIGHT_BLUE    =  9,
   COLOR_BRIGHT_GREEN   = 10,
   COLOR_BRIGHT_CYAN    = 11,
   COLOR_BRIGHT_RED     = 12,
   COLOR_BRIGHT_MAGENTA = 13,
   COLOR_BRIGHT_YELLOW  = 14,
   COLOR_BRIGHT_WHITE   = 15,
};

#define make_color(fg, bg) ((u8)(((fg) | (bg) << 4)))
#define get_color_fg(color) ((color) & 0xF)
#define get_color_bg(color) (((color) >> 4) & 0xF)

#if CONSOLE_DEFAULT_BRIGHT_WHITE
   #define DEFAULT_FG_COLOR COLOR_BRIGHT_WHITE
#else
   #define DEFAULT_FG_COLOR COLOR_WHITE
#endif

#define DEFAULT_BG_COLOR COLOR_BLACK

/*
 * Entry defs (color + char): the hardware format (VGA textmode) is used also
 * by the hw-independent subsystems "term" and "fb_console", for convenience.
 */
#define make_vgaentry(c, color) ((u16)(((u16)c) | ((u16)color << 8)))
#define vgaentry_get_fg(e)      LO_BITS((e) >> 8, 4, u8)
#define vgaentry_get_bg(e)      LO_BITS((e) >> 12, 4, u8)
#define vgaentry_get_char(e)    LO_BITS((e), 8, u8)
#define vgaentry_get_color(e)   SHR_BITS((e), 8, u8)
