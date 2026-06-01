/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

// Defining some necessary symbols just to make the linker happy.

void *kernel_initial_stack = NULL;

void asm_save_regs_and_schedule(void *unused) { NOT_REACHED(); }
void switch_to_initial_kernel_stack(void) { NOT_REACHED(); }
void fault_resumable_call(u32 faults_mask,
                          void *func,
                          u32 nargs,
                          ...)
{
   NOT_REACHED();
}

/*
 * Real (working) fake: copy_{to,from}_user() call this and there are no page
 * faults in the host environment, so a plain byte copy is the correct behavior.
 */
int arch_user_copy(void *dest, const void *src, size_t n)
{
   char *d = dest;
   const char *s = src;
   size_t i;

   for (i = 0; i < n; i++)
      d[i] = s[i];

   return 0;
}

/* Address-only landing pad in the real kernel; never reached on the host. */
void asm_user_copy_fault(void) { NOT_REACHED(); }

void asm_do_bogomips_loop(void) { NOT_REACHED(); }
void asm_nop_loop(void) { NOT_REACHED(); }
void context_switch(void) { NOT_REACHED(); }
