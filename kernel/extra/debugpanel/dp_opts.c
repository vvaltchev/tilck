/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/fb_console.h>
#include "termutil.h"

#ifdef RELEASE
   #define IS_RELEASE_BUILD 1
#else
   #define IS_RELEASE_BUILD 0
#endif

#define DUMP_STR_OPT(r, c, opt)    dp_write(r, c, "%-30s: %s", #opt, opt)
#define DUMP_INT_OPT(r, c, opt)    dp_write(r, c, "%-30s: %d", #opt, opt)
#define DUMP_BOOL_OPT(r, c, opt)   dp_write(r, c, "%-30s: %u", #opt, opt)

#define DUMP_INT(r, c, name, val)  \
   dp_write(r, c, DP_ESC_COLOR "%-16s: %d", name, val)

static void dp_show_opts(void)
{
   const int row = term_get_curr_row(get_curr_term()) + 1;
   const int rcol = dp_start_col + 47;

   dp_draw_rect(row, dp_start_col + 1, 19, 45);
   dp_write(row, dp_start_col + 3,
            ESC_COLOR_GREEN "[ Build-time ]" RESET_ATTRS);

   dp_start_col++;

   DUMP_INT_OPT(row+1, 0, IS_RELEASE_BUILD);
   DUMP_STR_OPT(row+2, 0, BUILDTYPE_STR);

   // Non-boolean kernel options
   DUMP_INT_OPT(row+3, 0, TIMER_HZ);
   DUMP_INT_OPT(row+4, 0, USER_STACK_PAGES);

   // Boolean options ENABLED by default
   DUMP_BOOL_OPT(row+5, 0, KRN_TRACK_NESTED_INTERR);
   DUMP_BOOL_OPT(row+6, 0, PANIC_SHOW_STACKTRACE);
   DUMP_BOOL_OPT(row+7, 0, DEBUG_CHECKS_IN_RELEASE);
   DUMP_BOOL_OPT(row+8, 0, KERNEL_SELFTESTS);

   // Boolean options DISABLED by default
   DUMP_BOOL_OPT(row+9, 0, KERNEL_GCOV);
   DUMP_BOOL_OPT(row+10, 0, FORK_NO_COW);
   DUMP_BOOL_OPT(row+11, 0, MMAP_NO_COW);
   DUMP_BOOL_OPT(row+12, 0, PANIC_SHOW_REGS);
   DUMP_BOOL_OPT(row+13, 0, KMALLOC_FREE_MEM_POISONING);
   DUMP_BOOL_OPT(row+14, 0, KMALLOC_SUPPORT_DEBUG_LOG);
   DUMP_BOOL_OPT(row+15, 0, KMALLOC_SUPPORT_LEAK_DETECTOR);
   DUMP_BOOL_OPT(row+16, 0, BOOTLOADER_POISON_MEMORY);

   dp_start_col--;

   dp_draw_rect(row, rcol, 19, 28);
   dp_write(row+0, rcol+2, ESC_COLOR_GREEN "[ Boot-time ]" RESET_ATTRS);

   DUMP_INT(row+1, rcol+2, "TERM_ROWS", term_get_rows(get_curr_term()));
   DUMP_INT(row+2, rcol+2, "TERM_COLS", term_get_cols(get_curr_term()));
   DUMP_INT(row+3, rcol+2, "USE_FRAMEBUFFER", use_framebuffer());
   DUMP_INT(row+4, rcol+2, "FB_OPT_FUNCS", fb_is_using_opt_funcs());
   DUMP_INT(row+5, rcol+2, "FB_RES_X", fb_get_res_x());
   DUMP_INT(row+6, rcol+2, "FB_RES_Y", fb_get_res_y());
   DUMP_INT(row+7, rcol+2, "FB_BBP", fb_get_bbp());
   DUMP_INT(row+8, rcol+2, "FB_FONT_W", fb_get_font_w());
   DUMP_INT(row+9, rcol+2, "FB_FONT_H", fb_get_font_h());
}

static dp_screen dp_opts_screen =
{
   .index = 0,
   .label = "Options",
   .draw_func = dp_show_opts,
   .on_keypress_func = NULL,
};

__attribute__((constructor))
static void dp_opts_init(void)
{
   dp_register_screen(&dp_opts_screen);
}
