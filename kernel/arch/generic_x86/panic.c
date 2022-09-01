/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/atomics.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/system_mmap.h>

volatile bool __in_panic;
volatile bool __in_double_fault;
volatile bool __in_kernel_shutdown;
volatile bool __in_panic_debugger;

void init_console(void);         /* defined in main.c */
void panic_save_current_state(); /* defined in kernel_yield.S */
regs_t panic_state_regs;

/* Called by the assembly function panic_save_current_state() */
void panic_save_current_task_state(regs_t *r)
{
   /*
    * Clear the higher (unused) bits of the segment registers for a nicer
    * panic regs_t dump.
    */

#if NBITS == 32
   r->ss &= 0xffff;
   r->cs &= 0xffff;
   r->ds &= 0xffff;
   r->es &= 0xffff;
   r->fs &= 0xffff;
   r->gs &= 0xffff;
#else
   // TODO: add code here
#endif

   /*
    * Since in panic we need just to save the state without doing a context
    * switch, just saving the ESP in state_regs won't work, because
    * we'll going to continue using the same stack. In this particular corner
    * case, just store the regs in a static regs_t instance.
    */

   memcpy(&panic_state_regs, r, sizeof(regs_t));
   struct task *curr = get_curr_task();

   if (curr)
      curr->state_regs = &panic_state_regs;
}

static void disable_fpu_features(void)
{
   x86_cpu_features.can_use_sse = false;
   x86_cpu_features.can_use_sse2 = false;
   x86_cpu_features.can_use_avx = false;
   x86_cpu_features.can_use_avx2 = false;
}

static void panic_print_task_info(struct task *curr)
{
   const char *str;

   if (curr && curr != kernel_process && curr->tid != -1) {

      if (!is_kernel_thread(curr)) {

         printk("Current task [USER]: tid: %i, pid: %i\n",
                curr->tid, curr->pi->pid);

      } else {

         str = curr->kthread_name;
         printk("Current task [KERNEL]: tid: %i [%s]\n",
                curr->tid, str ? str : "???");
      }

   } else {
      printk("Current task: NONE\n");
   }
}

static int
stop_all_other_tasks(void *task, void *unused)
{
   struct task *ti = task;

   if (ti != get_curr_task()) {
      ti->stopped = true;
   }

   return 0;
}

NORETURN void panic(const char *fmt, ...)
{
   static bool first_printk_ok;
   static bool first_in_panic_double_fault;
   static const char *saved_fmt;
   static va_list saved_args;

   ulong rc;
   va_list args;
   struct task *curr;
   bool panic_triggered_df = false;

   if (!kopt_panic_kb) {

      /* No interrupts: we're in a panic state */
      disable_interrupts_forced();

   } else {

      /*
       * While it's a good idea to disable ALL the interrupts during panic(),
       * sometimes the panic code path does not affect screen scrolling nor
       * the console, nor the PS/2 driver. Therefore, in those cases, it's
       * great to reproduce the bug and being able to scroll.
       *
       * Therefore, let's mask all the IRQs except the IRQ for the PS/2
       * keyboard.
       */

      int keep = kopt_sercon ? X86_PC_COM1_COM3_IRQ : X86_PC_KEYBOARD_IRQ;

      disable_interrupts_forced();

      for (int irq = 0; irq < 16; irq++) {

         if (irq != keep)
            irq_set_mask(irq);
      }

      disable_preemption();
      iterate_over_tasks(&stop_all_other_tasks, NULL);
      enable_interrupts_forced();
   }

   force_enable_preemption();   /* Set a predefined and sane state */

   if (__in_panic) {

      /* Ouch, nested panic! */

      if (!__in_double_fault || first_in_panic_double_fault) {

         /*
          * If we're NOT in double fault or we're in a double fault occurred
          * BEFORE the first call to panic(), we're in a pathologic panic loop
          * and there's nothing more we can do.
          */

         if (first_printk_ok)
            printk("[panic] Got panic while in panic state. Halt.\n");

         goto end;
      }

      /*
       * In this case we're in double fault caused by panic() itself because of
       * stack overflow. Since the double fault handler uses a separate stack,
       * we can continue.
       */

      panic_triggered_df = true;
   }

   __in_panic = true;
   first_in_panic_double_fault = __in_double_fault;

   va_start(args, fmt);

   if (!saved_fmt) {
      saved_fmt = fmt;
      va_copy(saved_args, args);
   }

   disable_fpu_features();

   if (!__in_double_fault)
      panic_save_current_state();

   curr = get_curr_task();

   if (term_is_initialized()) {

      if (get_curr_tty() != NULL)
         tty_setup_for_panic(get_curr_tty());

      /* In case the video output has been paused, we MUST restart it */
      term_restart_output();

   } else {

      init_console();
   }

   /* Hopefully, we can show something on the screen */
   printk("\n********************** KERNEL PANIC **********************\n");

   /*
    * Register the fact that the first printk() succeeded: in case of panic
    * in panic, at least we know that we can show something on the screen.
    */

   first_printk_ok = true;

   /* print the arguments in a fault-safe way */
   rc = fault_resumable_call(ALL_FAULTS_MASK, vprintk, 2, fmt, args);

   if (rc != 0) {
      printk("[panic] Got fault %d while trying to print "
             "the panic message.", get_fault_num(rc));
   }

   va_end(args);
   printk("\n");

   if (panic_triggered_df) {

      printk("[panic] The double fault occurred in panic (stack overflow)\n");
      printk("[panic] Original panic message: <<\n");

      /* print the arguments in a fault-safe way */
      rc = fault_resumable_call(ALL_FAULTS_MASK,
                                vprintk,
                                2, saved_fmt, saved_args);

      if (rc != 0) {
         printk("[panic] Got fault %d while trying to print "
                "the panic message.", get_fault_num(rc));
      }

      printk(">>\n");
   }

   panic_print_task_info(curr);
   panic_dump_nested_interrupts();

   if (__in_double_fault) {
      copy_main_tss_on_regs(&panic_state_regs);
      if (!kopt_panic_regs)
         printk("SP: %p\n", regs_get_stack_ptr(&panic_state_regs));
   }

   if (kopt_panic_regs)
      dump_regs(&panic_state_regs);

   if (!kopt_panic_nobt) {
      dump_stacktrace(
         __in_double_fault
            ? regs_get_frame_ptr(&panic_state_regs)
            : NULL,
         curr->pi->pdir
      );
   }

   if (kopt_panic_mmap)
      dump_memory_map();

   if (DEBUG_QEMU_EXIT_ON_PANIC)
      debug_qemu_turn_off_machine();

   if (kopt_panic_kb) {
      __in_panic_debugger = true;
      sys_tilck_cmd(TILCK_CMD_DEBUGGER_TOOL, 0, 0, 0, 0);
   }

end:
   /* Halt the CPU forever */
   disable_interrupts_forced();
   while (true) { halt(); }
}
