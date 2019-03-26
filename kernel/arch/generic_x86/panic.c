/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
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

void panic_save_current_state(); /* defined in kernel_yield.S */

static regs panic_state_regs;

/* Called the assembly function panic_save_current_state() */
void panic_save_current_task_state(regs *r)
{
   /*
    * Clear the higher (unused) bits of the segment registers for a nicer
    * panic regs dump.
    */
   r->ss &= 0xffff;
   r->cs &= 0xffff;
   r->ds &= 0xffff;
   r->es &= 0xffff;
   r->fs &= 0xffff;
   r->gs &= 0xffff;

   /*
    * Since in panic we need just to save the state without doing a context
    * switch, just saving the ESP in state_regs won't work, because
    * we'll going to continue using the same stack. In this particular corner
    * case, just store the regs a static regs instance.
    */

   memcpy(&panic_state_regs, r, sizeof(regs));
   task_info *curr = get_curr_task();

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

static void panic_print_task_info(task_info *curr)
{
   const char *str;

   if (curr && curr != kernel_process && curr->tid != -1) {

      if (!is_kernel_thread(curr)) {

         printk("Current task [USER]: tid: %i, pid: %i\n",
                curr->tid, curr->pi->pid);

      } else {

         str = find_sym_at_addr_safe((uptr)curr->what, NULL, NULL);
         printk("Current task [KERNEL]: tid: %i [%s]\n",
                curr->tid, str ? str : "???");
      }

   } else {
      printk("Current task: NONE\n");
   }
}

NORETURN void panic(const char *fmt, ...)
{
   static bool first_printk_ok;

   uptr rc;
   va_list args;
   task_info *curr;

   disable_interrupts_forced(); /* No interrupts: we're in a panic state */

   if (__in_panic) {

      /* Ouch, nested panic! */

      if (first_printk_ok)
         printk("FATAL: got panic while in panic state. Halt.\n");

      goto end;
   }

   __in_panic = true;

   disable_fpu_features();
   panic_save_current_state();
   curr = get_curr_task();

   if (term_is_initialized(get_curr_term())) {

      if (get_curr_tty() != NULL)
         tty_setup_for_panic(get_curr_tty());

   } else {

      init_console();
   }

   /* Hopefully, we can print something on screen */

   printk("*********************************"
          " KERNEL PANIC "
          "********************************\n");

   /*
    * Register the fact that the first printk() succeeded: in case of panic
    * in panic, at least we know that we can show something on the screen.
    */

   first_printk_ok = true;

   va_start(args, fmt);

   /* print the arguments in a fault-safe way */
   rc = fault_resumable_call(ALL_FAULTS_MASK, vprintk, 2, fmt, args);

   if (rc != 0) {
      printk("[panic] Got fault %d while trying to print "
             "the panic message.", get_fault_num(rc));
   }

   va_end(args);
   printk("\n");

   panic_print_task_info(curr);
   panic_dump_nested_interrupts();

   if (PANIC_SHOW_REGS)
      dump_regs(&panic_state_regs);

   if (PANIC_SHOW_STACKTRACE)
      dump_stacktrace();

   if (DEBUG_QEMU_EXIT_ON_PANIC)
      debug_qemu_turn_off_machine();

end:
   /* Halt the CPU forever */
   while (true) { halt(); }
}
