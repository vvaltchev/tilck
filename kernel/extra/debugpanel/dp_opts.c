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


#define SHOW_INT(name, val)  dp_printk(DP_COLOR "%-20s: %d", name, val)
#define DUMP_STR_OPT(opt)    dp_printkln(DP_COLOR "%-30s: %s", #opt, opt)
#define DUMP_INT_OPT(opt)    dp_printkln(DP_COLOR "%-30s: %d", #opt, opt)
#define DUMP_BOOL_OPT(opt)   dp_printkln(DP_COLOR "%-30s: %u", #opt, opt)

void dp_show_opts(void)
{
   const int row = term_get_curr_row(get_curr_term()) + 1;

   dp_printkln("[         Build-time options         ]");

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

   dp_move_cursor(row, 50);
   dp_printk("[ Boot-time options ]");

   dp_move_cursor(row + 1, 50);
   SHOW_INT("TERM_ROWS", term_get_rows(get_curr_term()));

   dp_move_cursor(row + 2, 50);
   SHOW_INT("TERM_COLS", term_get_cols(get_curr_term()));

   dp_move_cursor(row + 3, 50);
   SHOW_INT("USE_FRAMEBUFFER", use_framebuffer());

   dp_move_cursor(row + 4, 50);
   SHOW_INT("FB_OPT_FUNCS", fb_is_using_opt_funcs());

   dp_move_cursor(row + 5, 50);
   SHOW_INT("FB_RES_X", fb_get_res_x());

   dp_move_cursor(row + 6, 50);
   SHOW_INT("FB_RES_Y", fb_get_res_y());

   dp_move_cursor(row + 7, 50);
   SHOW_INT("FB_BBP", fb_get_bbp());

   dp_move_cursor(row + 8, 50);
   SHOW_INT("FB_FONT_W", fb_get_font_w());

   dp_move_cursor(row + 9, 50);
   SHOW_INT("FB_FONT_H", fb_get_font_h());
}
