/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/term.h>
#include "gfx_chars.h"

#pragma GCC diagnostic push

const u8 fg_csi_to_vga[256] =
{
   [30] = COLOR_BLACK,
   [31] = COLOR_RED,
   [32] = COLOR_GREEN,
   [33] = COLOR_YELLOW,
   [34] = COLOR_BLUE,
   [35] = COLOR_MAGENTA,
   [36] = COLOR_CYAN,
   [37] = COLOR_WHITE,
   [90] = COLOR_BRIGHT_BLACK,
   [91] = COLOR_BRIGHT_RED,
   [92] = COLOR_BRIGHT_GREEN,
   [93] = COLOR_BRIGHT_YELLOW,
   [94] = COLOR_BRIGHT_BLUE,
   [95] = COLOR_BRIGHT_MAGENTA,
   [96] = COLOR_BRIGHT_CYAN,
   [97] = COLOR_BRIGHT_WHITE,
};


#ifdef __clang__
   #pragma GCC diagnostic ignored "-Winitializer-overrides"
#else
   #pragma GCC diagnostic ignored "-Woverride-init"
#endif

/* clang-format off */
const s16 tty_default_trans_table[256] =
{
   [0 ... 31] = -1,        /* not translated by default */

   [32] = 32,     [56] = 56,     [80] = 80,     [104] = 104,
   [33] = 33,     [57] = 57,     [81] = 81,     [105] = 105,
   [34] = 34,     [58] = 58,     [82] = 82,     [106] = 106,
   [35] = 35,     [59] = 59,     [83] = 83,     [107] = 107,
   [36] = 36,     [60] = 60,     [84] = 84,     [108] = 108,
   [37] = 37,     [61] = 61,     [85] = 85,     [109] = 109,
   [38] = 38,     [62] = 62,     [86] = 86,     [110] = 110,
   [39] = 39,     [63] = 63,     [87] = 87,     [111] = 111,
   [40] = 40,     [64] = 64,     [88] = 88,     [112] = 112,
   [41] = 41,     [65] = 65,     [89] = 89,     [113] = 113,
   [42] = 42,     [66] = 66,     [90] = 90,     [114] = 114,
   [43] = 43,     [67] = 67,     [91] = 91,     [115] = 115,
   [44] = 44,     [68] = 68,     [92] = 92,     [116] = 116,
   [45] = 45,     [69] = 69,     [93] = 93,     [117] = 117,
   [46] = 46,     [70] = 70,     [94] = 94,     [118] = 118,
   [47] = 47,     [71] = 71,     [95] = 95,     [119] = 119,
   [48] = 48,     [72] = 72,     [96] = 96,     [120] = 120,
   [49] = 49,     [73] = 73,     [97] = 97,     [121] = 121,
   [50] = 50,     [74] = 74,     [98] = 98,     [122] = 122,
   [51] = 51,     [75] = 75,     [99] = 99,     [123] = 123,
   [52] = 52,     [76] = 76,     [100] = 100,   [124] = 124,
   [53] = 53,     [77] = 77,     [101] = 101,   [125] = 125,
   [54] = 54,     [78] = 78,     [102] = 102,   [126] = 126,
   [55] = 55,     [79] = 79,     [103] = 103,

   [127 ... 255] = -1,     /* not translated */
};
/* clang-format on */

const s16 tty_gfx_trans_table[256] =
{
   [0 ... 255] = -1,       /* not translated by default */

   [' '] = ' ',
   ['l'] = CHAR_ULCORNER,
   ['m'] = CHAR_LLCORNER,
   ['k'] = CHAR_URCORNER,
   ['j'] = CHAR_LRCORNER,
   ['t'] = CHAR_LTEE,
   ['u'] = CHAR_RTEE,
   ['v'] = CHAR_BTEE,
   ['w'] = CHAR_TTEE,
   ['q'] = CHAR_HLINE,
   ['x'] = CHAR_VLINE,
   ['n'] = CHAR_CROSS,
   ['`'] = CHAR_DIAMOND,
   ['a'] = CHAR_BLOCK_MID,
   ['f'] = CHAR_DEGREE,
   ['g'] = CHAR_PLMINUS,
   ['~'] = CHAR_BULLET,
   [','] = CHAR_LARROW,
   ['+'] = CHAR_RARROW,
   ['.'] = CHAR_DARROW,
   ['-'] = CHAR_UARROW,
   ['h'] = CHAR_BLOCK_LIGHT,
   ['0'] = CHAR_BLOCK_HEAVY,
};

#pragma GCC diagnostic pop
