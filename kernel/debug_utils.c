/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/system_mmap.h>

#include <elf.h>         // system header
#include <multiboot.h>   // system header in include/system_headers

volatile bool __in_panic;

#define DUMP_STR_OPT(opt)  printk(NO_PREFIX "%-35s: %s\n", #opt, opt)
#define DUMP_INT_OPT(opt)  printk(NO_PREFIX "%-35s: %d\n", #opt, opt)
#define DUMP_BOOL_OPT(opt) printk(NO_PREFIX "%-35s: %u\n", #opt, opt)

void debug_show_build_opts(void)
{
   printk(NO_PREFIX "\n");
   printk(NO_PREFIX "------------------- BUILD OPTIONS ------------------\n");
#ifdef RELEASE
   DUMP_INT_OPT(RELEASE);
#endif

   DUMP_STR_OPT(BUILDTYPE_STR);
   DUMP_INT_OPT(TIMER_HZ);
   DUMP_BOOL_OPT(KERNEL_TRACK_NESTED_INTERRUPTS);
   DUMP_BOOL_OPT(KERNEL_GCOV);
   DUMP_BOOL_OPT(FORK_NO_COW);
   DUMP_BOOL_OPT(MMAP_NO_COW);
   DUMP_BOOL_OPT(PANIC_SHOW_STACKTRACE);
   DUMP_BOOL_OPT(PANIC_SHOW_REGS);
   DUMP_BOOL_OPT(KMALLOC_FREE_MEM_POISONING);
   DUMP_BOOL_OPT(KMALLOC_SUPPORT_DEBUG_LOG);
   DUMP_BOOL_OPT(KMALLOC_SUPPORT_LEAK_DETECTOR);
   DUMP_BOOL_OPT(KMALLOC_HEAPS_CREATION_DEBUG);
   DUMP_BOOL_OPT(BOOTLOADER_POISON_MEMORY);
   DUMP_BOOL_OPT(DEBUG_CHECKS_IN_RELEASE_BUILD);
   printk(NO_PREFIX "\n");
}

static void debug_dump_slow_irq_handler_count(void)
{
   extern u32 slow_timer_irq_handler_count;

   if (KERNEL_TRACK_NESTED_INTERRUPTS) {
      printk(NO_PREFIX "   Slow timer irq handler counter: %u\n",
             slow_timer_irq_handler_count);
   }
}

static void debug_dump_spur_irq_count(void)
{
   extern u32 spur_irq_count;
   const u64 ticks = get_ticks();

   if (ticks > TIMER_HZ)
      printk(NO_PREFIX "   Spurious IRQ count: %u (%u/sec)\n",
             spur_irq_count,
             spur_irq_count / (ticks / TIMER_HZ));
   else
      printk(NO_PREFIX "   Spurious IRQ count: %u (< 1 sec)\n",
             spur_irq_count, spur_irq_count);
}

static void debug_dump_unhandled_irq_count(void)
{
   extern u32 unhandled_irq_count[256];
   u32 tot_count = 0;

   for (u32 i = 0; i < ARRAY_SIZE(unhandled_irq_count); i++)
      tot_count += unhandled_irq_count[i];

   if (!tot_count)
      return;

   printk(NO_PREFIX "\n");
   printk(NO_PREFIX "Unhandled IRQs count table\n\n");

   for (u32 i = 0; i < ARRAY_SIZE(unhandled_irq_count); i++) {

      if (unhandled_irq_count[i])
         printk(NO_PREFIX "   IRQ #%3u: %3u unhandled\n", i,
                unhandled_irq_count[i]);
   }

   printk(NO_PREFIX "\n");
}

void debug_show_spurious_irq_count(void)
{
   printk(NO_PREFIX "\n");
   printk(NO_PREFIX "Kernel IRQ-related counters\n\n");

   debug_dump_slow_irq_handler_count();
   debug_dump_spur_irq_count();
   debug_dump_unhandled_irq_count();
}

#ifndef UNIT_TEST_ENVIRONMENT

static int debug_f_key_press_handler(u32 key, u8 c)
{
   if (!kb_is_ctrl_pressed())
      return KB_HANDLER_NAK;

   switch (key) {

      case KEY_F1:
         debug_show_build_opts();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F2:
         debug_kmalloc_dump_mem_usage();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F3:
         dump_system_memory_map();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F4:
         debug_show_task_list();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F5:
         debug_show_spurious_irq_count();
         return KB_HANDLER_OK_AND_STOP;

      default:
         return KB_HANDLER_NAK;
   }
}

void register_debug_kernel_keypress_handler(void)
{
   if (kb_register_keypress_handler(&debug_f_key_press_handler) < 0)
      panic("Unable to register debug Fn keypress handler");
}

#endif // UNIT_TEST_ENVIRONMENT
