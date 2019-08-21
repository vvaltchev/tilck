/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include "dp_int.h"

#define ERASE_DISPLAY      "\033[2J"
#define REVERSE_VIDEO      "\033[7m"
#define ESC_COLOR_WHITE    "\033[97m"
#define ESC_COLOR_RED      "\033[91m"
#define ESC_COLOR_GREEN    "\033[32m"
#define ESC_COLOR_YELLOW   "\033[93m"
#define RESET_ATTRS        "\033[0m"
#define GFX_ON             "\033(0"
#define GFX_OFF            "\033(B"

#define TERM_VLINE         GFX_ON "x" GFX_OFF
#define DP_ESC_COLOR       ESC_COLOR_WHITE

void dp_write(int row, int col, const char *fmt, ...);
void dp_draw_rect(int row, int col, int h, int w);

static inline void dp_move_right(int n) {
   printk(NO_PREFIX "\033[%dC", n);
}

static inline void dp_move_left(int n) {
   printk(NO_PREFIX "\033[%dD", n);
}

static inline void dp_move_to_col(int n) {
   printk(NO_PREFIX "\033[%dG", n);
}

static inline void dp_clear(void) {
   printk(NO_PREFIX ERASE_DISPLAY);
}

static inline void dp_move_cursor(int row, int col)
{
   printk(NO_PREFIX "\033[%d;%dH", row, col);
}

static inline const char *
dp_sign_value_esc_color(sptr val)
{
   return val > 0
            ? ESC_COLOR_GREEN
            : val < 0 ? ESC_COLOR_RED : DP_ESC_COLOR;
}
