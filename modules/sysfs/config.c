/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>
#include <tilck_gen_headers/config_boot.h>
#include <tilck_gen_headers/config_kmalloc.h>
#include <tilck_gen_headers/config_userlim.h>
#include <tilck_gen_headers/config_mm.h>
#include <tilck_gen_headers/config_sched.h>
#include <tilck_gen_headers/mod_kb8042.h>
#include <tilck_gen_headers/mod_console.h>
#include <tilck_gen_headers/mod_fb.h>
#include <tilck_gen_headers/mod_serial.h>
#include <tilck_gen_headers/mod_debugpanel.h>
#include <tilck_gen_headers/mod_tracing.h>
#include <tilck_gen_headers/mod_sysfs.h>
#include <tilck_gen_headers/mod_acpi.h>
#include <tilck_gen_headers/mod_pci.h>
#include <tilck_gen_headers/mod_sb16.h>

#include <tilck/common/build_info.h>

#include <tilck/mods/sysfs.h>
#include <tilck/mods/sysfs_utils.h>


/* config */
DEF_STATIC_CONF_RO(STRING, buildtype,              BUILDTYPE_STR);
DEF_STATIC_CONF_RO(STRING, version,                tilck_build_info.ver);
DEF_STATIC_CONF_RO(STRING, arch,                   tilck_build_info.arch);
DEF_STATIC_CONF_RO(STRING, commit,                 tilck_build_info.commit);
DEF_STATIC_CONF_RO(BOOL,   debugchecks,            DEBUG_CHECKS);

/* config/kernel */
DEF_STATIC_CONF_RO(ULONG, timer_hz,                TIMER_HZ);
DEF_STATIC_CONF_RO(ULONG, time_slice_ticks,        TIME_SLICE_TICKS);
DEF_STATIC_CONF_RO(ULONG, stack_pages,             KERNEL_STACK_PAGES);
DEF_STATIC_CONF_RO(ULONG, user_stack_pages,        USER_STACK_PAGES);
DEF_STATIC_CONF_RO(BOOL,  track_nested_int,        KRN_TRACK_NESTED_INTERR);
DEF_STATIC_CONF_RO(BOOL,  panic_backtrace,         PANIC_SHOW_STACKTRACE);
DEF_STATIC_CONF_RO(BOOL,  panic_regs,              PANIC_SHOW_REGS);
DEF_STATIC_CONF_RO(BOOL,  selftests,               KERNEL_SELFTESTS);
DEF_STATIC_CONF_RO(BOOL,  stack_isolation,         KERNEL_STACK_ISOLATION);
DEF_STATIC_CONF_RO(BOOL,  symbols,                 KERNEL_SYMBOLS);
DEF_STATIC_CONF_RO(BOOL,  printk_on_curr_tty,      KRN_PRINTK_ON_CURR_TTY);
DEF_STATIC_CONF_RO(BOOL,  resched_enable_preempt,  KRN_RESCHED_ENABLE_PREEMPT);
DEF_STATIC_CONF_RO(BOOL,  big_io_buf,              KERNEL_BIG_IO_BUF);
DEF_STATIC_CONF_RO(BOOL,  gcov,                    KERNEL_GCOV);
DEF_STATIC_CONF_RO(BOOL,  fork_no_cow,             FORK_NO_COW);
DEF_STATIC_CONF_RO(BOOL,  mmap_no_cow,             MMAP_NO_COW);
DEF_STATIC_CONF_RO(BOOL,  ubsan,                   KERNEL_UBSAN);
DEF_STATIC_CONF_RO(BOOL,  kernel_64bit_offt,       KERNEL_64BIT_OFFT);
DEF_STATIC_CONF_RO(BOOL,  clock_drift_comp,        KRN_CLOCK_DRIFT_COMP);

/* config/console */
DEF_STATIC_CONF_RO(ULONG, big_font_threshold,      FBCON_BIGFONT_THR);
DEF_STATIC_CONF_RO(BOOL,  banner,                  FB_CONSOLE_BANNER);
DEF_STATIC_CONF_RO(BOOL,  cursor_blink,            FB_CONSOLE_CURSOR_BLINK);
DEF_STATIC_CONF_RO(BOOL,  use_alt_fonts,           FB_CONSOLE_USE_ALT_FONTS);
DEF_STATIC_CONF_RO(BOOL,  show_logo,               KERNEL_SHOW_LOGO);
DEF_STATIC_CONF_RO(BOOL,  big_scroll_buf,          TERM_BIG_SCROLL_BUF);
DEF_STATIC_CONF_RO(BOOL,  failsafe_opt,            FB_CONSOLE_FAILSAFE_OPT);

/* config/modules */
DEF_STATIC_CONF_RO(BOOL,  sysfs,                   MOD_sysfs);
DEF_STATIC_CONF_RO(BOOL,  pci,                     MOD_pci);
DEF_STATIC_CONF_RO(BOOL,  acpi,                    MOD_acpi);
DEF_STATIC_CONF_RO(BOOL,  kb8042,                  MOD_kb8042);
DEF_STATIC_CONF_RO(BOOL,  tracing,                 MOD_tracing);
DEF_STATIC_CONF_RO(BOOL,  console,                 MOD_console);
DEF_STATIC_CONF_RO(BOOL,  fb,                      MOD_fb);
DEF_STATIC_CONF_RO(BOOL,  serial,                  MOD_serial);
DEF_STATIC_CONF_RO(BOOL,  sb16,                    MOD_sb16);
DEF_STATIC_CONF_RO(BOOL,  debugpanel,              MOD_debugpanel);

void sysfs_create_config_obj(void)
{
   struct sysobj *config, *kernel, *console, *modules;

   config = sysfs_create_custom_obj(
      "config",
      NULL,       /* hooks */
      SYSOBJ_CONF_PROP_PAIR(buildtype),
      SYSOBJ_CONF_PROP_PAIR(version),
      SYSOBJ_CONF_PROP_PAIR(arch),
      SYSOBJ_CONF_PROP_PAIR(commit),
      SYSOBJ_CONF_PROP_PAIR(debugchecks),
      NULL
   );

   if (!config)
      goto fail;

   if (sysfs_register_obj(NULL, &sysfs_root_obj, "config", config))
      goto fail;

   kernel = sysfs_create_custom_obj(
      "kernel",
      NULL,       /* hooks */
      SYSOBJ_CONF_PROP_PAIR(timer_hz),
      SYSOBJ_CONF_PROP_PAIR(time_slice_ticks),
      SYSOBJ_CONF_PROP_PAIR(stack_pages),
      SYSOBJ_CONF_PROP_PAIR(user_stack_pages),
      SYSOBJ_CONF_PROP_PAIR(track_nested_int),
      SYSOBJ_CONF_PROP_PAIR(panic_backtrace),
      SYSOBJ_CONF_PROP_PAIR(panic_regs),
      SYSOBJ_CONF_PROP_PAIR(selftests),
      SYSOBJ_CONF_PROP_PAIR(stack_isolation),
      SYSOBJ_CONF_PROP_PAIR(symbols),
      SYSOBJ_CONF_PROP_PAIR(printk_on_curr_tty),
      SYSOBJ_CONF_PROP_PAIR(resched_enable_preempt),
      SYSOBJ_CONF_PROP_PAIR(big_io_buf),
      SYSOBJ_CONF_PROP_PAIR(gcov),
      SYSOBJ_CONF_PROP_PAIR(fork_no_cow),
      SYSOBJ_CONF_PROP_PAIR(mmap_no_cow),
      SYSOBJ_CONF_PROP_PAIR(ubsan),
      SYSOBJ_CONF_PROP_PAIR(kernel_64bit_offt),
      SYSOBJ_CONF_PROP_PAIR(clock_drift_comp),
      NULL
   );

   if (!kernel)
      goto fail;

   if (sysfs_register_obj(NULL, config, "kernel", kernel))
      goto fail;

   console = sysfs_create_custom_obj(
      "console",
      NULL,       /* hooks */
      SYSOBJ_CONF_PROP_PAIR(big_font_threshold),
      SYSOBJ_CONF_PROP_PAIR(banner),
      SYSOBJ_CONF_PROP_PAIR(cursor_blink),
      SYSOBJ_CONF_PROP_PAIR(use_alt_fonts),
      SYSOBJ_CONF_PROP_PAIR(show_logo),
      SYSOBJ_CONF_PROP_PAIR(big_scroll_buf),
      SYSOBJ_CONF_PROP_PAIR(failsafe_opt),
      NULL
   );

   if (!console)
      goto fail;

   if (sysfs_register_obj(NULL, config, "console", console))
      goto fail;

   modules = sysfs_create_custom_obj(
      "modules",
      NULL,       /* hooks */
      SYSOBJ_CONF_PROP_PAIR(sysfs),
      SYSOBJ_CONF_PROP_PAIR(pci),
      SYSOBJ_CONF_PROP_PAIR(acpi),
      SYSOBJ_CONF_PROP_PAIR(kb8042),
      SYSOBJ_CONF_PROP_PAIR(tracing),
      SYSOBJ_CONF_PROP_PAIR(console),
      SYSOBJ_CONF_PROP_PAIR(fb),
      SYSOBJ_CONF_PROP_PAIR(serial),
      SYSOBJ_CONF_PROP_PAIR(sb16),
      SYSOBJ_CONF_PROP_PAIR(debugpanel),
      NULL
   );

   if (!modules)
      goto fail;

   if (sysfs_register_obj(NULL, config, "modules", modules))
      goto fail;

   /* Success */
   return;

fail:
   panic("Unable to create the sysfs config obj");
}
