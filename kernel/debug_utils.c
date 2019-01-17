/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/atomics.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>

volatile bool __in_panic;

#ifndef UNIT_TEST_ENVIRONMENT

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/tty.h>

#include <elf.h>
#include <multiboot.h>

void panic_save_current_state(); /* defined in kernel_yield.S */

NORETURN void panic(const char *fmt, ...)
{
   disable_interrupts_forced(); /* No interrupts: we're in a panic state */

   if (__in_panic)
      goto end;

   __in_panic = true;

   x86_cpu_features.can_use_sse = false;
   x86_cpu_features.can_use_sse2 = false;
   x86_cpu_features.can_use_avx = false;
   x86_cpu_features.can_use_avx2 = false;

   panic_save_current_state();

   if (term_is_initialized()) {

      if (term_get_filter_func() != NULL)
         tty_setup_for_panic();

   } else {

      init_console();
   }


   printk("*********************************"
          " KERNEL PANIC "
          "********************************\n");

   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
   va_end(args);

   printk("\n");

   task_info *curr = get_curr_task();

   if (curr && curr != kernel_process && curr->tid != -1) {
      if (!is_kernel_thread(curr)) {
         printk("Current task [USER]: tid: %i, pid: %i\n",
                curr->tid, curr->pid);
      } else {
         ptrdiff_t off;
         const char *s = find_sym_at_addr_safe((uptr)curr->what, &off, NULL);
         printk("Current task [KERNEL]: tid: %i [%s]\n",
                curr->tid, s ? s : "???");
      }
   } else {
      printk("Current task: NONE\n");
   }

   panic_dump_nested_interrupts();

   if (PANIC_SHOW_REGS)
      dump_regs(curr->state_regs);

   if (PANIC_SHOW_STACKTRACE)
      dump_stacktrace();

   if (DEBUG_QEMU_EXIT_ON_PANIC)
      debug_qemu_turn_off_machine();

end:

   while (true) {
      halt();
   }
}

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

int debug_f_key_press_handler(u32 key, u8 c)
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

#endif // UNIT_TEST_ENVIRONMENT
