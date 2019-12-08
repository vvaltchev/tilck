/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_modules.h>
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/term.h>

char zero_page[PAGE_SIZE] ALIGNED_AT(PAGE_SIZE);

#if KERNEL_SYMBOLS
char symtab_buf[SYMTAB_MAX_SIZE] __attribute__ ((section (".Symtab"))) = {0};
char strtab_buf[STRTAB_MAX_SIZE] __attribute__ ((section (".Strtab"))) = {0};
#else
char symtab_buf[1] __attribute__ ((section (".Symtab"))) = {0};
char strtab_buf[1] __attribute__ ((section (".Strtab"))) = {0};
#endif

bool __use_framebuffer;

#ifdef DEBUG

const uptr init_st_begin = (uptr)&kernel_initial_stack;
const uptr init_st_end   = (uptr)&kernel_initial_stack + KERNEL_STACK_SIZE;

void validate_stack_pointer_int(const char *file, int line)
{
   uptr stack_var = 123;
   const uptr stack_var_page = (uptr)&stack_var & PAGE_MASK;
   const uptr st_begin = (uptr)get_curr_task()->kernel_stack;
   const uptr st_end = st_begin + KERNEL_STACK_SIZE;

   if (IN_RANGE(stack_var_page, init_st_begin, init_st_end)) {

      /*
       * That's fine: we are in the initialization or in task_switch() called
       * by sys_exit().
       */
      return;
   }

   if (!IN_RANGE(stack_var_page, st_begin, st_end)) {

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

#if KERNEL_SHOW_LOGO

static void print_banner_line(const u8 *s)
{
   printk(NO_PREFIX "\033(0");

   for (const u8 *p = s; *p; p++) {
      printk(NO_PREFIX "%c", *p);
   }

   printk(NO_PREFIX "\033(B");
   printk(NO_PREFIX "\n");
}

void show_tilck_logo(void)
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

   struct term_params tparams;
   term_read_info(&tparams);
   const u32 cols = tparams.cols;
   const u32 padding = (u32)(cols / 2 - strlen(banner[1]) / 2);

   for (u32 i = 0; i < ARRAY_SIZE(banner); i++) {

      for (u32 j = 0; j < padding; j++)
         printk(NO_PREFIX " ");

      print_banner_line((u8 *)banner[i]);
   }
}

#endif
