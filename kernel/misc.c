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

static void print_banner_line(const u8 *s)
{
   printk(NO_PREFIX "\033(0");

   for (const u8 *p = s; *p; p++) {
      printk(NO_PREFIX "%c", *p);
   }

   printk(NO_PREFIX "\033(B");
   printk(NO_PREFIX "\n");
}

void show_banner(void)
{
   char *banner[] =
   {
      "",
      "aaaaaaaak aak aak       aaaaaak aak  aak",
      "mqqaalqqj aax aax      aalqqqqj aax aalj",
      "   aax    aax aax      aax      aaaaalj ",
      "   aax    aax aax      aax      aalqaak ",
      "   aax    aax aaaaaaak maaaaaak aax  aak",
      "   mqj    mqj mqqqqqqj  mqqqqqj mqj  mqj",
      "",
   };

   const u32 padding = (u32)
      (term_get_cols(get_curr_term()) / 2 - strlen(banner[1]) / 2);

   for (u32 i = 0; i < ARRAY_SIZE(banner); i++) {

      for (u32 j = 0; j < padding; j++)
         printk(NO_PREFIX " ");

      print_banner_line((u8 *)banner[i]);
   }
}
