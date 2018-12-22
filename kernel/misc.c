/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/term.h>

char symtab_buf[SYMTAB_MAX_SIZE] __attribute__ ((section (".Symtab"))) = {0};
char strtab_buf[STRTAB_MAX_SIZE] __attribute__ ((section (".Strtab"))) = {0};

#ifdef DEBUG

void validate_stack_pointer_int(const char *file, int line)
{
   uptr stack_var = 123;
   const uptr stack_var_page = (uptr)&stack_var & PAGE_MASK;

   if (stack_var_page == (uptr)&kernel_initial_stack) {

      /*
       * That's fine: we are in the initialization or in task_switch() called
       * by sys_exit().
       */
      return;
   }

   if (stack_var_page != (uptr)get_curr_task()->kernel_stack) {

      panic("Invalid kernel stack pointer.\n"
            "File %s at line %i\n"
            "[validate stack] stack page: %p\n"
            "[validate stack] expected:   %p\n",
            file, line,
            ((uptr)&stack_var & PAGE_MASK),
            get_curr_task()->kernel_stack);
   }
}

#endif


static const u8 console_gfx_replacements[256] =
{
   ['#'] = CHAR_BLOCK_MID,
   ['-'] = CHAR_HLINE,
   ['|'] = CHAR_VLINE,
   ['+'] = CHAR_CROSS,
   ['A'] = CHAR_CORNER_UL,
   ['B'] = CHAR_CORNER_UR,
   ['C'] = CHAR_CORNER_LR,
   ['D'] = CHAR_CORNER_LL
};

void console_gfx_replace_chars(char *str)
{
   for (u8 *p = (u8 *)str; *p; p++)
      if (console_gfx_replacements[*p])
         *p = console_gfx_replacements[*p];
}

void show_banner(void)
{
   char *banner[] =
   {
      "",
      "########B ##B ##B       ######B ##B  ##B",
      "D--##A--C ##| ##|      ##A----C ##| ##AC",
      "   ##|    ##| ##|      ##|      #####AC ",
      "   ##|    ##| ##|      ##|      ##A-##B ",
      "   ##|    ##| #######B D######B ##|  ##B",
      "   D-C    D-C D------C  D-----C D-C  D-C",
      ""
   };

   const u32 padding = term_get_cols() / 2 - strlen(banner[1]) / 2;

   for (u32 i = 0; i < ARRAY_SIZE(banner); i++)
      console_gfx_replace_chars(banner[i]);

   for (u32 i = 0; i < ARRAY_SIZE(banner); i++) {

      for (u32 j = 0; j < padding; j++)
         printk(NO_PREFIX " ");

      printk(NO_PREFIX "%s\n", banner[i]);
   }
}
