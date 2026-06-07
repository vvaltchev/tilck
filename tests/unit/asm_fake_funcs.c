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
 * Working fake for copy_{to,from}_user() and the string copies. By default it
 * just byte-copies (there are no real page faults on the host). To exercise
 * the kernel's fault-recovery paths, set_user_copy_fault() arms a byte range
 * the fake treats as an unmapped user page: any access overlapping it "faults"
 * (returns 1). `fault_after` lets that many overlapping accesses through first,
 * so a re-read of the same address can fault after an earlier read succeeded.
 */
static ulong uc_fault_lo;
static ulong uc_fault_hi;
static int uc_fault_after = -1;        /* < 0: never fault */
static int uc_fault_seen;

void set_user_copy_fault(ulong lo, ulong hi, int fault_after)
{
   uc_fault_lo = lo;
   uc_fault_hi = hi;
   uc_fault_after = fault_after;
   uc_fault_seen = 0;
}

int arch_user_copy(void *dest, const void *src, size_t n)
{
   const ulong s = (ulong)src;
   const ulong d = (ulong)dest;
   char *cd = dest;
   const char *cs = src;
   bool overlap;
   size_t i;

   overlap = uc_fault_lo < uc_fault_hi &&
             ((s < uc_fault_hi && s + n > uc_fault_lo) ||
              (d < uc_fault_hi && d + n > uc_fault_lo));

   if (overlap && uc_fault_after >= 0) {
      if (uc_fault_seen++ >= uc_fault_after)
         return 1;                     /* simulate the unmapped-page fault */
   }

   for (i = 0; i < n; i++)
      cd[i] = cs[i];

   return 0;
}

/* Address-only landing pad in the real kernel; never reached on the host. */
void asm_user_copy_fault(void) { NOT_REACHED(); }

void asm_do_bogomips_loop(void) { NOT_REACHED(); }
void asm_nop_loop(void) { NOT_REACHED(); }
void context_switch(void) { NOT_REACHED(); }
