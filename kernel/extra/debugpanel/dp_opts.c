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

#define DUMP_STR_OPT(opt)    dp_write(row++, col, "%-30s: %s", #opt, opt)
#define DUMP_INT_OPT(opt)    dp_write(row++, col, "%-30s: %d", #opt, opt)
#define DUMP_BOOL_OPT(opt)   dp_write(row++, col, "%-30s: %u", #opt, opt)
#define DUMP_INT(name, val)  dp_write(row++, col, "%-16s: %d", name, val)

static void dp_show_opts(void)
{
   int row = dp_screen_start_row + 1;
   int col = dp_start_col + 3;
   int rows_left;
   int rows_right;
   int max_rows;

   DUMP_INT_OPT(IS_RELEASE_BUILD);
   DUMP_STR_OPT(BUILDTYPE_STR);

   // Non-boolean kernel options
   DUMP_INT_OPT(TIMER_HZ);
   DUMP_INT_OPT(USER_STACK_PAGES);

   // Boolean options ENABLED by default
   DUMP_BOOL_OPT(KRN_TRACK_NESTED_INTERR);
   DUMP_BOOL_OPT(PANIC_SHOW_STACKTRACE);
   DUMP_BOOL_OPT(DEBUG_CHECKS_IN_RELEASE);
   DUMP_BOOL_OPT(KERNEL_SELFTESTS);

   // Boolean options DISABLED by default
   DUMP_BOOL_OPT(KERNEL_GCOV);
   DUMP_BOOL_OPT(FORK_NO_COW);
   DUMP_BOOL_OPT(MMAP_NO_COW);
   DUMP_BOOL_OPT(PANIC_SHOW_REGS);
   DUMP_BOOL_OPT(KMALLOC_FREE_MEM_POISONING);
   DUMP_BOOL_OPT(KMALLOC_SUPPORT_DEBUG_LOG);
   DUMP_BOOL_OPT(KMALLOC_SUPPORT_LEAK_DETECTOR);
   DUMP_BOOL_OPT(BOOTLOADER_POISON_MEMORY);

   rows_left = row - dp_screen_start_row - 1;
   row = dp_screen_start_row+1;
   col = dp_start_col + 49;

   DUMP_INT("TERM_ROWS", term_get_rows(get_curr_term()));
   DUMP_INT("TERM_COLS", term_get_cols(get_curr_term()));
   DUMP_INT("USE_FRAMEBUFFER", use_framebuffer());
   DUMP_INT("FB_OPT_FUNCS", fb_is_using_opt_funcs());
   DUMP_INT("FB_RES_X", fb_get_res_x());
   DUMP_INT("FB_RES_Y", fb_get_res_y());
   DUMP_INT("FB_BBP", fb_get_bbp());
   DUMP_INT("FB_FONT_W", fb_get_font_w());
   DUMP_INT("FB_FONT_H", fb_get_font_h());

   rows_right = row - dp_screen_start_row - 1;
   max_rows = MAX(rows_left, rows_right);

   /* left rectangle */
   dp_draw_rect("Build-time",
                dp_screen_start_row, dp_start_col + 1, max_rows+2, 45);

   /* right rectangle */
   dp_draw_rect("Boot-time",
                dp_screen_start_row, col - 2, max_rows+2, 28);
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
