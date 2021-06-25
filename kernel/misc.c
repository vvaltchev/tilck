/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>
#include <tilck_gen_headers/mod_console.h>
#include <tilck_gen_headers/modules_list.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/compiler.h>
#include <tilck/common/build_info.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/term.h>

char zero_page[PAGE_SIZE] ALIGNED_AT(PAGE_SIZE);

#if KERNEL_SYMBOLS
char symtab_buf[SYMTAB_MAX_SIZE] ATTR_SECTION(".Symtab") = {0};
char strtab_buf[STRTAB_MAX_SIZE] ATTR_SECTION(".Strtab") = {0};
#else
char symtab_buf[1] ATTR_SECTION(".Symtab") = {0};
char strtab_buf[1] ATTR_SECTION(".Strtab") = {0};
#endif

bool __use_framebuffer;

#if DEBUG_CHECKS
const ulong init_st_begin = (ulong)&kernel_initial_stack;
const ulong init_st_end   = (ulong)&kernel_initial_stack + KERNEL_STACK_SIZE;
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

static void show_tilck_logo(void)
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
   process_term_read_info(&tparams);
   const u32 cols = tparams.cols;
   const u32 padding = (u32)(cols / 2 - strlen(banner[1]) / 2);

   for (int i = 0; i < ARRAY_SIZE(banner); i++) {

      for (u32 j = 0; j < padding; j++)
         printk(NO_PREFIX " ");

      print_banner_line((u8 *)banner[i]);
   }
}

static void
show_system_info(void)
{
   const int time_slice = 1000 / (TIMER_HZ / TIME_SLICE_TICKS);
   const char *in_hyp_str = in_hypervisor() ? "yes" : "no";

   printk("timer_hz: \e[1m%i\e[m", TIMER_HZ);
   printk("; time_slice: \e[1m%i\e[m", time_slice);
   printk(" ms; in_hypervisor: \e[1m%s\e[m\n", in_hyp_str);
}

void
show_hello_message(void)
{
   struct commit_hash_and_date comm;
   extract_commit_hash_and_date(&tilck_build_info, &comm);

   if (VER_PATCH > 0)
      printk("Hello from Tilck \e[1m%d.%d.%d\e[m",
             VER_MAJOR, VER_MINOR, VER_PATCH);
   else
      printk("Hello from Tilck \e[1m%d.%d\e[m",
             VER_MAJOR, VER_MINOR);

   printk(", commit: \e[1m%s\e[m", comm.hash);

   if (comm.dirty)
      printk(" (dirty)");
   else if (comm.tags[0])
      printk(" (%s)", comm.tags);

   printk("\n");
   printk("Build type: \e[1m%s\e[m", BUILDTYPE_STR);
   printk(", compiler: \e[1m%s %d.%d.%d\e[m\n",
          COMPILER_NAME,
          COMPILER_MAJOR, COMPILER_MINOR, COMPILER_PATCHLEVEL);

   show_system_info();

   if (KERNEL_SHOW_LOGO)
      show_tilck_logo();
}

WEAK const char *get_signal_name(int signum) {
   return "";
}

const struct build_info tilck_build_info ATTR_SECTION(".tilck_info") = {
   .commit = {0}, /* It will get patched after the build */
   .ver = VER_MAJOR_STR "." VER_MINOR_STR "." VER_PATCH_STR,
   .arch = ARCH_GCC_TC,
   .modules_list = ENABLED_MODULES_LIST,
};
