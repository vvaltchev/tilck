/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>

#define NESTED_FAULTING_CODE_MAX_LEVELS 4

#ifdef __i386__

static void faulting_code_div0(void)
{
   asmVolatile("mov $0, %edx\n\t"
               "mov $1, %eax\n\t"
               "mov $0, %ecx\n\t"
               "div %ecx\n\t");
}

static void faulting_code(void)
{
   printk("hello from div by 0 faulting code\n");

   disable_preemption();

   faulting_code_div0();

   /*
    * Note: because the above asm will trigger a div by 0 fault, we'll never
    * reach the enable_preemption() below. This is an intentional way of testing
    * that fault_resumable_call() will restore correctly the value of
    * disable_preemption_count in case of fault.
    */

   enable_preemption();
}

static void faulting_code2(void)
{
#ifndef __clang_analyzer__

   /*
    * The static analyzer cannot possible imagine that we want intentionally
    * to trigger a PAGE FAULT and check that it has been handeled correctly.
    */

   ulong *ptr = NULL;
   bzero(ptr, sizeof(ulong));

#endif
}

static void nested_faulting_code(int level)
{
   if (level == NESTED_FAULTING_CODE_MAX_LEVELS) {
      printk("[level %i]: *** call faulting code ***\n", level);
      faulting_code2();
      NOT_REACHED();
   }

   printk("[level %i]: do recursive nested call\n", level);

   u32 r = fault_resumable_call(ALL_FAULTS_MASK,      // mask
                                nested_faulting_code, // func
                                1,                    // #args
                                level + 1);           // arg1

   if (level == NESTED_FAULTING_CODE_MAX_LEVELS - 1)
      VERIFY(r == 1 << FAULT_PAGE_FAULT);
   else if (level == NESTED_FAULTING_CODE_MAX_LEVELS - 2)
      VERIFY(r == 1 << FAULT_DIVISION_BY_ZERO);
   else if (level == NESTED_FAULTING_CODE_MAX_LEVELS - 3)
      VERIFY(r == 0);

   if (r) {
      if (level == NESTED_FAULTING_CODE_MAX_LEVELS - 1) {
         printk("[level %i]: the call faulted (r = %u). "
                "Let's do another faulty call\n", level, r);
         faulting_code_div0();
         NOT_REACHED();
      } else {
         printk("[level %i]: the call faulted (r = %u)\n", level, r);
      }
   } else {
      printk("[level %i]: the call was OK\n", level);
   }

   printk("[level %i]: we reached the end\n", level);
}

void selftest_fault_res_short(void)
{
   u32 r;

   printk("fault_resumable with just printk()\n");
   r = fault_resumable_call(ALL_FAULTS_MASK,
                            printk,
                            2,
                            "hi from fault resumable: %s\n",
                            "arg1");
   printk("returned %u\n", r);
   VERIFY(r == 0);

   printk("fault_resumable with code causing div by 0\n");
   r = fault_resumable_call(1 << FAULT_DIVISION_BY_ZERO, faulting_code, 0);
   printk("returned %u\n", r);
   VERIFY(r == 1 << FAULT_DIVISION_BY_ZERO);

   printk("fault_resumable with code causing page fault\n");
   r = fault_resumable_call(1 << FAULT_PAGE_FAULT, faulting_code2, 0);
   printk("returned %u\n", r);
   VERIFY(r == 1 << FAULT_PAGE_FAULT);

   printk("[level 0]: do recursive nested call\n");
   r = fault_resumable_call(ALL_FAULTS_MASK, // all faults
                            nested_faulting_code,
                            1,  // nargs
                            1); // arg1: level
   printk("[level 0]: call returned %u\n", r);
   VERIFY(r == 0);
   regular_self_test_end();
}

static NO_INLINE void do_nothing(ulong a1, ulong a2, ulong a3,
                                 ulong a4, ulong a5, ulong a6)
{
   DO_NOT_OPTIMIZE_AWAY(a1);
   DO_NOT_OPTIMIZE_AWAY(a2);
   DO_NOT_OPTIMIZE_AWAY(a3);
   DO_NOT_OPTIMIZE_AWAY(a4);
   DO_NOT_OPTIMIZE_AWAY(a5);
   DO_NOT_OPTIMIZE_AWAY(a6);
}

void selftest_fault_res_perf_short(void)
{
   const int iters = 100000;
   u64 start, duration;

   start = RDTSC();

   for (int i = 0; i < iters; i++)
      do_nothing(1,2,3,4,5,6);

   duration = RDTSC() - start;

   printk("regular call: %llu cycles\n", duration/iters);

   start = RDTSC();

   for (int i = 0; i < iters; i++)
      fault_resumable_call(0, do_nothing, 6, 1, 2, 3, 4, 5, 6);

   duration = RDTSC() - start;

   printk("fault resumable call: %llu cycles\n", duration/iters);
   regular_self_test_end();
}

#endif
