/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#define ERASE_DISPLAY "\033[2J"
#define REVERSE_VIDEO "\033[7m"
#define COLOR_WHITE   "\033[97m"
#define COLOR_RED     "\033[91m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[93m"
#define RESET_ATTRS   "\033[0m"

#define DP_COLOR                COLOR_WHITE
#define dp_printk(...)          printk(NO_PREFIX COLOR_WHITE __VA_ARGS__)

static inline void dp_clear(void) {
   printk(NO_PREFIX ERASE_DISPLAY);
}

static inline void dp_move_cursor(int row, int col)
{
   printk(NO_PREFIX "\033[%d;%dH", row, col);
}

static inline void dp_write_header(int i, const char *s, bool selected)
{
   if (selected) {
      dp_printk(DP_COLOR "%d" REVERSE_VIDEO "[%s]" RESET_ATTRS " ", i, s);
   } else {
      dp_printk(DP_COLOR "%d[%s]" RESET_ATTRS " ", i, s);
   }
}
