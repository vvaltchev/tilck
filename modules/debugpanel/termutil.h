/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/color_defs.h>
#include "dp_int.h"

#define ERASE_DISPLAY            "\033[2J"
#define REVERSE_VIDEO            "\033[7m"
#define E_COLOR_WHITE            "\033[37m"
#define E_COLOR_BR_WHITE         "\033[97m"
#define E_COLOR_RED              "\033[31m"
#define E_COLOR_BR_RED           "\033[91m"
#define E_COLOR_GREEN            "\033[32m"
#define E_COLOR_BR_GREEN         "\033[92m"
#define E_COLOR_BROWN            "\033[33m"
#define E_COLOR_YELLOW           "\033[93m"
#define E_COLOR_BLUE             "\033[34m"
#define E_COLOR_BR_BLUE          "\033[94m"
#define E_COLOR_MAGENTA          "\033[35m"
#define E_COLOR_CYAN             "\033[36m"
#define E_COLOR_BR_CYAN          "\033[96m"
#define E_COLOR_WHITE_ON_RED     "\033[97;41m"
#define ATTR_BOLD                "\033[1m"
#define RESET_ATTRS              "\033[0m"
#define GFX_ON                   "\033(0"
#define GFX_OFF                  "\033(B"
#define HIDE_CURSOR              "\033[?25l"
#define SHOW_CURSOR              "\033[?25h"
#define USE_ALT_BUF              "\033[?1049h"
#define USE_DEF_BUF              "\033[?1049l"

#define TERM_VLINE               GFX_ON "x" GFX_OFF
#define DP_COLOR                 make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR)
#define DP_REV_COLOR             make_color(DEFAULT_BG_COLOR, DEFAULT_FG_COLOR)
#define DP_ESC_COLOR             E_COLOR_WHITE

#define DP_KEY_BACKSPACE         0x7f
#define DP_KEY_ESC               0x1b
#define DP_KEY_ENTER             0x0d
#define DP_KEY_CTRL_C            0x03
#define DP_KEY_CTRL_T            0x14

void dp_write(int row, int col, const char *fmt, ...);

void dp_draw_rect(const char *label,
                  const char *esc_label_color,
                  int row,
                  int col,
                  int h,
                  int w);

/* WARNING: dirty macro expecting a local `row` variable to be defined */
#define dp_writeln(...)  dp_write(row++, 0, __VA_ARGS__)

/* WARNING: dirty macro expecting both `row` and `col` to be defined */
#define dp_writeln2(...) dp_write(row++, col, __VA_ARGS__)

void dp_reverse_colors(void);
void dp_reset_attrs(void);
void dp_move_right(int n);
void dp_move_left(int n);
void dp_move_to_col(int n);
void dp_clear(void);
void dp_move_cursor(int row, int col);
void dp_set_cursor_enabled(bool enabled);
void dp_switch_to_alt_buffer(void);
void dp_switch_to_default_buffer(void);
void dp_show_modal_msg(const char *msg);

static inline const char *
dp_sign_value_esc_color(long val)
{
   return val > 0
            ? E_COLOR_GREEN
            : val < 0 ? E_COLOR_BR_RED : DP_ESC_COLOR;
}

/* raw functions, avoid using when possible */
void dp_write_raw(const char *fmt, ...);
void dp_draw_rect_raw(int row, int col, int h, int w);
void dp_write_raw_int(const char *buf, int len);
