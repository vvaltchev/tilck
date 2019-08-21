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

#define L_DUMP_STR_OPT(opt)    dp_printkln(DP_COLOR "%-30s: %s", #opt, opt)
#define L_DUMP_INT_OPT(opt)    dp_printkln(DP_COLOR "%-30s: %d", #opt, opt)
#define L_DUMP_BOOL_OPT(opt)   dp_printkln(DP_COLOR "%-30s: %u", #opt, opt)

#define R_SHOW_INT(name, val)  dp_printk(DP_COLOR "%-16s: %d", name, val)

static void dp_show_opts(void)
{
   const int row = term_get_curr_row(get_curr_term()) + 1;
   const int right_col = dp_start_col + 47;

   dp_draw_rect(row, dp_start_col + 1, 19, 45);
   dp_move_cursor(row, dp_start_col + 1 + 2);
   dp_printk(ESC_COLOR_GREEN "[ Build-time ]" RESET_ATTRS "\n");
   dp_start_col++;

   L_DUMP_INT_OPT(IS_RELEASE_BUILD);
   L_DUMP_STR_OPT(BUILDTYPE_STR);

   // Non-boolean kernel options
   L_DUMP_INT_OPT(TIMER_HZ);
   L_DUMP_INT_OPT(USER_STACK_PAGES);

   // Boolean options ENABLED by default
   L_DUMP_BOOL_OPT(KRN_TRACK_NESTED_INTERR);
   L_DUMP_BOOL_OPT(PANIC_SHOW_STACKTRACE);
   L_DUMP_BOOL_OPT(DEBUG_CHECKS_IN_RELEASE);
   L_DUMP_BOOL_OPT(KERNEL_SELFTESTS);

   // Boolean options DISABLED by default
   L_DUMP_BOOL_OPT(KERNEL_GCOV);
   L_DUMP_BOOL_OPT(FORK_NO_COW);
   L_DUMP_BOOL_OPT(MMAP_NO_COW);
   L_DUMP_BOOL_OPT(PANIC_SHOW_REGS);
   L_DUMP_BOOL_OPT(KMALLOC_FREE_MEM_POISONING);
   L_DUMP_BOOL_OPT(KMALLOC_SUPPORT_DEBUG_LOG);
   L_DUMP_BOOL_OPT(KMALLOC_SUPPORT_LEAK_DETECTOR);
   L_DUMP_BOOL_OPT(BOOTLOADER_POISON_MEMORY);

   dp_start_col--;

   dp_draw_rect(row, right_col, 19, 28);
   dp_move_cursor(row, right_col + 2);
   dp_printk(ESC_COLOR_GREEN "[ Boot-time ]" RESET_ATTRS);

   dp_move_cursor(row + 1, right_col + 2);
   R_SHOW_INT("TERM_ROWS", term_get_rows(get_curr_term()));

   dp_move_cursor(row + 2, right_col + 2);
   R_SHOW_INT("TERM_COLS", term_get_cols(get_curr_term()));

   dp_move_cursor(row + 3, right_col + 2);
   R_SHOW_INT("USE_FRAMEBUFFER", use_framebuffer());

   dp_move_cursor(row + 4, right_col + 2);
   R_SHOW_INT("FB_OPT_FUNCS", fb_is_using_opt_funcs());

   dp_move_cursor(row + 5, right_col + 2);
   R_SHOW_INT("FB_RES_X", fb_get_res_x());

   dp_move_cursor(row + 6, right_col + 2);
   R_SHOW_INT("FB_RES_Y", fb_get_res_y());

   dp_move_cursor(row + 7, right_col + 2);
   R_SHOW_INT("FB_BBP", fb_get_bbp());

   dp_move_cursor(row + 8, right_col + 2);
   R_SHOW_INT("FB_FONT_W", fb_get_font_w());

   dp_move_cursor(row + 9, right_col + 2);
   R_SHOW_INT("FB_FONT_H", fb_get_font_h());
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
