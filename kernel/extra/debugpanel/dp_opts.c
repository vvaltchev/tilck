/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/fb_console.h>
#include "termutil.h"

#ifdef RELEASE
   #define RELEASE_INT 1
#else
   #define RELEASE_INT 0
#endif


#define SHOW_INT(name, val)  dp_printk(DP_COLOR "%-32s: %d\n", name, val)
#define DUMP_STR_OPT(opt)    dp_printk(DP_COLOR "%-32s: %s\n", #opt, opt)
#define DUMP_INT_OPT(opt)    dp_printk(DP_COLOR "%-32s: %d\n", #opt, opt)
#define DUMP_BOOL_OPT(opt)   dp_printk(DP_COLOR "%-32s: %u\n", #opt, opt)

void dp_show_opts(void)
{
   dp_printk(DP_COLOR "\n\n");

   DUMP_INT_OPT(RELEASE_INT);
   DUMP_STR_OPT(BUILDTYPE_STR);

   // Non-boolean kernel options
   DUMP_INT_OPT(TIMER_HZ);
   DUMP_INT_OPT(USER_STACK_PAGES);

   // Boolean options ENABLED by default
   DUMP_BOOL_OPT(KERNEL_TRACK_NESTED_INTERRUPTS);
   DUMP_BOOL_OPT(PANIC_SHOW_STACKTRACE);
   DUMP_BOOL_OPT(DEBUG_CHECKS_IN_RELEASE_BUILD);
   DUMP_BOOL_OPT(KERNEL_SELFTESTS);

   // Boolean options DISABLED by default
   DUMP_BOOL_OPT(KERNEL_GCOV);
   DUMP_BOOL_OPT(FORK_NO_COW);
   DUMP_BOOL_OPT(MMAP_NO_COW);
   DUMP_BOOL_OPT(PANIC_SHOW_REGS);
   DUMP_BOOL_OPT(KMALLOC_FREE_MEM_POISONING);
   DUMP_BOOL_OPT(KMALLOC_SUPPORT_DEBUG_LOG);
   DUMP_BOOL_OPT(KMALLOC_SUPPORT_LEAK_DETECTOR);
   DUMP_BOOL_OPT(KMALLOC_HEAPS_CREATION_DEBUG);
   DUMP_BOOL_OPT(BOOTLOADER_POISON_MEMORY);

   dp_printk(DP_COLOR "\n");

// #ifndef UNIT_TEST_ENVIRONMENT

//    printk(NO_PREFIX "------------------- RUNTIME ------------------\n");
//    SHOW_INT("TERM_ROWS", term_get_rows(get_curr_term()));
//    SHOW_INT("TERM_COLS", term_get_cols(get_curr_term()));
//    SHOW_INT("USE_FRAMEBUFFER", use_framebuffer());
//    SHOW_INT("FB_OPT_FUNCS", fb_is_using_opt_funcs());
//    SHOW_INT("FB_RES_X", fb_get_res_x());
//    SHOW_INT("FB_RES_Y", fb_get_res_y());
//    SHOW_INT("FB_BBP", fb_get_bbp());
//    SHOW_INT("FB_FONT_W", fb_get_font_w());
//    SHOW_INT("FB_FONT_H", fb_get_font_h());
//    printk(NO_PREFIX "\n");

// #endif
}
