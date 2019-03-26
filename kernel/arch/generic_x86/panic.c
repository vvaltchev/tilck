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

void panic_save_current_state(); /* defined in kernel_yield.S */

NORETURN void panic(const char *fmt, ...)
{
   va_list args;
   ptrdiff_t off;
   const char *str;
   task_info *curr;

   disable_interrupts_forced(); /* No interrupts: we're in a panic state */

   if (__in_panic)
      goto end;

   __in_panic = true;
   curr = get_curr_task();

   x86_cpu_features.can_use_sse = false;
   x86_cpu_features.can_use_sse2 = false;
   x86_cpu_features.can_use_avx = false;
   x86_cpu_features.can_use_avx2 = false;

   if (!curr) {

      if (!kernel_process)
         create_kernel_process(); /* this is safe in panic */

      /*
       * We need to have __current != NULL because of functions like
       * panic_save_current_state() which set curr->regs.
       */
      set_curr_task(kernel_process);
      curr = kernel_process;
   }

   panic_save_current_state();

   if (term_is_initialized(get_curr_term())) {

      if (get_curr_tty() != NULL)
         tty_setup_for_panic(get_curr_tty());

   } else {

      init_console();
   }

   printk("*********************************"
          " KERNEL PANIC "
          "********************************\n");

   va_start(args, fmt);
   vprintk(fmt, args);
   va_end(args);

   printk("\n");

   if (curr != kernel_process && curr->tid != -1) {

      if (!is_kernel_thread(curr)) {

         printk("Current task [USER]: tid: %i, pid: %i\n",
                curr->tid, curr->pi->pid);

      } else {

         str = find_sym_at_addr_safe((uptr)curr->what, &off, NULL);
         printk("Current task [KERNEL]: tid: %i [%s]\n",
                curr->tid, str ? str : "???");
      }

   } else {
      printk("Current task: NONE\n");
   }

   panic_dump_nested_interrupts();

   if (PANIC_SHOW_REGS)
      if (curr->state_regs)
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
