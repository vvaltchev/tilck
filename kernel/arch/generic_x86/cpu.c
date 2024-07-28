/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/hal.h>

extern const char *x86_exception_names[32];

void asm_enable_osxsave(void);
void asm_enable_sse(void);
void asm_enable_avx(void);
static void handle_no_coproc_fault(regs_t *r);

#define CPU_FXSAVE_AREA_SIZE   512

/*
 * NOTE: calculating the exact area save area for XSAVE is tricky since it
 * depends on the features currently supported by the CPU (it is necessary
 * to iterate with CPUID calls to do the right calculation). For the moment,
 * just using a large buffer is a good-enough solution.
 *
 * TODO (future): calculate the exact size for the XSAVE area.
 */
#define CPU_XSAVE_AREA_SIZE   8192

static bool enable_sse(void)
{
   u32 res = fault_resumable_call(ALL_FAULTS_MASK, &asm_enable_sse, 0);

   if (res) {

      u32 n = get_first_set_bit_index32(res);

      printk("CPU: Enable SSE failed: fault %i [%s]\n",
             n, x86_exception_names[n]);

      return false;
   }

   const char *max_sse = "SSE 1";
   x86_cpu_features.can_use_sse = true;

   if (x86_cpu_features.edx1.sse2) {
      x86_cpu_features.can_use_sse2 = true;
      max_sse = "SSE 2";
   }

   if (x86_cpu_features.ecx1.sse3)
      max_sse = "SSE 3";

   if (x86_cpu_features.ecx1.ssse3)
      max_sse = "SSE 3+ (ssse 3)";

   if (x86_cpu_features.ecx1.sse4_1) {
      max_sse = "SSE 4.1";
      x86_cpu_features.can_use_sse4_1 = true;
   }

   if (x86_cpu_features.ecx1.sse4_2)
      max_sse = "SSE 4.2";

   printk("CPU: %s enabled\n", max_sse);
   return true;
}

static bool enable_osxsave(void)
{
   u32 res = fault_resumable_call(ALL_FAULTS_MASK, &asm_enable_osxsave, 0);

   if (res) {

      u32 n = get_first_set_bit_index32(res);

      printk("CPU: Enable OSXSAVE failed: fault %i [%s]\n",
             n, x86_exception_names[n]);

      return false;
   }

   return true;
}

static bool enable_avx(void)
{
   u32 res = fault_resumable_call(ALL_FAULTS_MASK, &asm_enable_avx, 0);

   if (res) {

      u32 n = get_first_set_bit_index32(res);

      printk("CPU: Enable AVX failed: fault %i [%s]\n",
             n, x86_exception_names[n]);

      return false;
   }

   x86_cpu_features.can_use_avx = true;

   if (x86_cpu_features.avx2) {
      x86_cpu_features.can_use_avx2 = true;
      printk("CPU: AVX 2 enabled\n");
   } else {
      printk("CPU: AVX 1 enabled\n");
   }

   return true;
}

void init_pat(void)
{
   u64 pat = rdmsr(MSR_IA32_PAT);
   u8 *entries = (u8 *)&pat;

   entries[7] = MEM_TYPE_WC;

   wrmsr(MSR_IA32_PAT, pat);
   printk("CPU: PAT initialized\n");
}

void enable_cpu_features(void)
{
   if (!x86_cpu_features.initialized)
      get_cpu_features();

   if (x86_cpu_features.edx1.sse && x86_cpu_features.edx1.fxsr) {
      if (!enable_sse())
         goto out;
   }

   if (x86_cpu_features.ecx1.xsave) {

      if (!enable_osxsave())
         goto out;

      if (x86_cpu_features.ecx1.avx)
         if (!enable_avx())
            goto out;
   }

out:
   /*
    * Disable the FPU (any access to any x87 FPU/MMX/SSE/AVX register).
    *
    *    1. This prevents the kernel to access any such register unintentionally
    *       (without using fpu_context_begin/fpu_context_end to save the regs).
    *
    *    2. This prevents user space to access any such register without the
    *       kernel knowing about it. When FPU regs are used with TS = 0, the
    *       CPU triggers a "No coprocessor" fault. If the kernel wants to allow
    *       this to happen[1], it will set a flag in the struct task struct,
    *       enable the FPU and resume the process (thread) as if nothing
    *       happened (like for the COW case). Otherwise, the kernel will send
    *       a SIGFPE to the "guilty" process.
    *
    * NOTES
    * ----------------
    * [1] In Tilck, we'll allow user space FPU only in the case at least FXSAVE
    * is available (=> SSE is available). This makes Tilck simpler allowing us
    * to not having to save the "legacy x87 FPU" context using the "legacy FPU"
    * instructions. The newer FXSAVE and XSAVE save everything, including the
    * "legacy x87 FPU" state.
    */
   hw_fpu_disable();
   set_fault_handler(FAULT_NO_COPROC, handle_no_coproc_fault);

   if (x86_cpu_features.edx1.mtrr)
      enable_mtrr();

   if (x86_cpu_features.edx1.pat)
      init_pat();

   printk("CPU: Physical addr bits: %u\n", x86_cpu_features.phys_addr_bits);
}

static char fpu_kernel_regs[CPU_XSAVE_AREA_SIZE] ALIGNED_AT(64);

void save_current_fpu_regs(bool in_kernel)
{
   if (UNLIKELY(!x86_cpu_features.can_use_sse))
      return;

   if (UNLIKELY(in_panic()))
      return;

   arch_task_members_t *arch_fields = get_task_arch_fields(get_curr_task());
   void *buf = in_kernel ? fpu_kernel_regs : arch_fields->fpu_regs;

   ASSERT(buf != NULL);

   if (x86_cpu_features.can_use_avx) {

      /*
       * In eax:edx we're supposed to specific which reg sets to save/restore
       * using a bitmask. Setting all bits to 1 works well to save/restore
       * "everything".
       */

      asmVolatile("xsave (%0)"
                  : /* no output */
                  : "r" (buf), "a" (-1), "d" (-1)
                  : /* no clobber */);
   } else {

      asmVolatile("fxsave (%0)"
                  : /* no output */
                  : "r" (buf)
                  : /* no clobber */);
   }
}

void restore_fpu_regs(void *task, bool in_kernel)
{
   if (UNLIKELY(!x86_cpu_features.can_use_sse))
      return;

   if (UNLIKELY(in_panic()))
      return;

   arch_task_members_t *arch_fields = get_task_arch_fields((struct task *)task);
   void *buf = in_kernel ? fpu_kernel_regs : arch_fields->fpu_regs;

   ASSERT(buf != NULL);

   if (x86_cpu_features.can_use_avx) {

      asmVolatile("xrstor (%0)"
                  : /* no output */
                  : "r" (buf), "a" (-1), "d" (-1)
                  : /* no clobber */);

   } else {

      asmVolatile("fxrstor (%0)"
                  : /* no output */
                  : "r" (buf)
                  : /* no clobber */);
   }
}

void restore_current_fpu_regs(bool in_kernel)
{
   restore_fpu_regs(get_curr_task(), in_kernel);
}

bool allocate_fpu_regs(arch_task_members_t *arch_fields)
{
   ASSERT(arch_fields->fpu_regs == NULL);

   if (x86_cpu_features.can_use_avx) {

      arch_fields->fpu_regs =
         aligned_kmalloc(CPU_XSAVE_AREA_SIZE, 8 * sizeof(void *));

      arch_fields->fpu_regs_size = CPU_XSAVE_AREA_SIZE;

   } else {

      arch_fields->fpu_regs =
         aligned_kmalloc(CPU_FXSAVE_AREA_SIZE, 4 * sizeof(void *));

      arch_fields->fpu_regs_size = CPU_FXSAVE_AREA_SIZE;
   }

   if (!arch_fields->fpu_regs)
      return false;

   bzero(arch_fields->fpu_regs, arch_fields->fpu_regs_size);
   return true;
}

static void
handle_no_coproc_fault(regs_t *r)
{
   if (is_kernel_thread(get_curr_task())) {
      panic("FPU instructions used in kernel outside an fpu_context!");
   }

   if (!x86_cpu_features.can_use_sse) {
      /*
       * TODO: send to the current process SIGFPE in this case.
       *
       * Probably for a long time, we won't support the use of legacy x87 FPU
       * instructions, in case the CPU is so old that it does not have SSE.
       * The reason is just to avoid implementing save/restore FPU registers
       * with x87 instructions. We already need to have 2 implementations:
       * for SSE (FXSAVE) and for AVX (XSAVE). Better avoid a 3rd one useful
       * only for machines produced before 1998.
       */

       panic("x87 FPU instructions not supported on CPUs without SSE");
   }

   arch_task_members_t *arch_fields = get_task_arch_fields(get_curr_task());
   ASSERT(!(r->custom_flags & REGS_FL_FPU_ENABLED));

#if FORK_NO_COW

   ASSERT(arch_fields->fpu_regs != NULL);

   /*
    * With the current implementation, even when the fpu_regs are
    * pre-allocated tasks cannot by default use the FPU. This approach has PROs
    * and CONs.
    *
    *    PROs:
    *       - the kernel doesn't have to save/restore unnecessarily the FPU ctx
    *       - tasks using the FPU can be distinguished from the others
    *       - it is simpler to make fpu_regs pre-allocated work this way
    *
    *    CONs:
    *       - paying the overhead of a not-strictly necessary fault, once.
    */

#else

    /*
     * We can hit this fault at MOST once in the lifetime of a task. These
     * sanity checks ensures that, in no case, we allocated the fpu_regs
     * and then, for any reason, we scheduled the task with FPU disabled.
     */

   ASSERT(arch_fields->fpu_regs == NULL);

   if (!allocate_fpu_regs(arch_fields)) {
      panic("Cannot allocate memory for the FPU context");
   }

#endif

   r->custom_flags |= REGS_FL_FPU_ENABLED;
}

static volatile bool in_fpu_context;

void fpu_context_begin(void)
{
   disable_preemption();

   /* NOTE: nested FPU contexts are NOT allowed (unless we're in panic) */

   if (LIKELY(!in_panic())) {
      ASSERT(!in_fpu_context);
   }

   in_fpu_context = true;
   hw_fpu_enable();
   save_current_fpu_regs(true);
}

void fpu_context_end(void)
{
   ASSERT(in_fpu_context);

   restore_current_fpu_regs(true);
   hw_fpu_disable();

   in_fpu_context = false;
   enable_preemption();
}
