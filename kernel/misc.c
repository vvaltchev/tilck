/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>
#include <tilck_gen_headers/mod_console.h>
#include <tilck_gen_headers/mod_fb.h>
#include <tilck_gen_headers/mod_ramfb.h>
#include <tilck_gen_headers/modules_list.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/compiler.h>
#include <tilck/common/build_info.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

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

#ifdef KERNEL_SHOW_LOGO
static void print_banner_line(const char *s)
{
   term_write("\033(0", 3, 0);

   for (const char *p = s; *p; p++) {
      term_write(p, 1, COLOR_GREEN);
   }

   term_write("\033(B", 3, COLOR_GREEN);
   term_write("\n", 1, COLOR_GREEN);
}

static void
show_tilck_logo(void)
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
         term_write(" ", 1, COLOR_GREEN);

      print_banner_line(banner[i]);
   }
}
#endif

static void
show_system_info(void)
{
   const int time_slice = 1000 / (TIMER_HZ / TIME_SLICE_TICKS);
   const char *in_hyp_str = in_hypervisor() ? "yes" : "no";

   printk("\e[32mtimer_hz: \e[m\e[1m%i\e[m\e[32m"
          "; time_slice: \e[m\e[1m%i\e[m\e[32m"
          " ms; in_hypervisor: \e[m\e[1m%s\e[m\n",
          TIMER_HZ, time_slice, in_hyp_str);
}

#ifdef __riscv64
#if MOD_ramfb && MOD_console && MOD_fb
static void
fb_show_no_keyboard_banner(void)
{
   extern ulong fb_vaddr;

   if (!fb_vaddr)
      return;

   char *banner[] =
   {
      "+-------------------- WARNING --------------------+\n",
      "|                                                 |\n",
      "| Keyboard input is NOT supported on RISC64, yet. |\n",
      "| Use the serial console.                         |\n",
      "|                                                 |\n",
      "+-------------------------------------------------+\n",
      "\n",
   };

   struct term_params tparams;
   process_term_read_info(&tparams);
   const u32 cols = tparams.cols;
   const u32 padding = (u32)(cols / 2 - strlen(banner[1]) / 2);

   for (int i = 0; i < ARRAY_SIZE(banner); i++) {

      for (u32 j = 0; j < padding; j++)
         term_write(" ", 1, COLOR_BLACK);

      term_write(banner[i], strlen(banner[i]), COLOR_BRIGHT_WHITE);
   }
}
#else
static void
fb_show_no_keyboard_banner(void)
{
   /* Do nothing */
}
#endif
#endif

void
show_hello_message(void)
{
   struct commit_hash_and_date comm;
   extract_commit_hash_and_date(&tilck_build_info, &comm);

   printk("\e[32mHello from Tilck \e[m\e[1m%d.%d.%d\e[m\e[32m, "
          "commit: \e[m\e[1m%s\e[m\e[32m (%s)\e[m\n",
          VER_MAJOR, VER_MINOR, VER_PATCH,
          comm.hash,
          comm.dirty
            ? "dirty"
            : comm.tags[0]
               ? comm.tags
               : "untagged");

   printk("\e[32mBuild type: \e[m\e[1m%s\e[m\e[32m, "
          "compiler: \e[m\e[1m%s %d.%d.%d\e[m\n",
          BUILDTYPE_STR,
          COMPILER_NAME,
          COMPILER_MAJOR,
          COMPILER_MINOR,
          COMPILER_PATCHLEVEL);

   show_system_info();

   if (KERNEL_SHOW_LOGO)
      show_tilck_logo();

#ifdef __riscv64
   if (MOD_ramfb && MOD_console && MOD_fb) {
      fb_show_no_keyboard_banner();
   }
#endif
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
