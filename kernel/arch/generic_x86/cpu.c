
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>
#include <exos/common/utils.h>
#include <exos/common/arch/generic_x86/cpu_features.h>

#include <exos/kernel/process.h>
#include <exos/kernel/fault_resumable.h>
#include <exos/kernel/interrupts.h>
#include <exos/kernel/hal.h>

extern const char *x86_exception_names[32];

void asm_enable_osxsave(void);
void asm_enable_sse(void);
void asm_enable_avx(void);
static void fpu_no_coprocessor_fault_handler(regs *r);

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
   u32 res = fault_resumable_call(~0, &asm_enable_sse, 0);

   if (res) {

      u32 n = get_first_set_bit_index(res);

      printk("Enable SSE failed: fault %i [%s]\n",
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

   printk("[CPU features] %s enabled\n", max_sse);
   return true;
}

static bool enable_osxsave(void)
{
   u32 res = fault_resumable_call(~0, &asm_enable_osxsave, 0);

   if (res) {

      u32 n = get_first_set_bit_index(res);

      printk("Enable OSXSAVE failed: fault %i [%s]\n",
             n, x86_exception_names[n]);

      return false;
   }

   return true;
}

static bool enable_avx(void)
{
   u32 res = fault_resumable_call(~0, &asm_enable_avx, 0);

   if (res) {

      u32 n = get_first_set_bit_index(res);

      printk("Enable AVX failed: fault %i [%s]\n",
             n, x86_exception_names[n]);

      return false;
   }

   x86_cpu_features.can_use_avx = true;

   if (x86_cpu_features.avx2) {
      x86_cpu_features.can_use_avx2 = true;
      printk("[CPU features] AVX 2 enabled\n");
   } else {
      printk("[CPU features] AVX 1 enabled\n");
   }

   return true;
}

void enable_cpu_features(void)
{
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
    *       this to happen[1], it will set a flag in the task_info struct,
    *       enable the FPU and resume the process (thread) as if nothing
    *       happened (like for the COW case). Otherwise, the kernel will send
    *       a SIGFPE to the "guilty" process.
    *
    * NOTES
    * ----------------
    * [1] In exOS, we'll allow user space FPU only in the case at least FXSAVE
    * is available (=> SSE is available). This makes exOS simpler allowing it to
    * not have to save the "legacy x87 FPU" context using the "legacy FPU"
    * instructions. The newer FXSAVE and XSAVE save everything, including the
    * "legacy FPU" state.
    */
   fpu_disable();
   set_fault_handler(FAULT_NO_COPROC, fpu_no_coprocessor_fault_handler);
}

static char fpu_kernel_regs[CPU_XSAVE_AREA_SIZE] __attribute__((aligned(64)));

void save_current_fpu_regs(bool in_kernel)
{
   if (UNLIKELY(!x86_cpu_features.can_use_sse))
      return;

   if (UNLIKELY(in_panic()))
      return;

   task_info *curr = get_curr_task();
   arch_task_info_members *arch_fields = &curr->arch;
   void *buf = in_kernel ? fpu_kernel_regs : arch_fields->fpu_regs;

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

void restore_current_fpu_regs(bool in_kernel)
{
   if (UNLIKELY(!x86_cpu_features.can_use_sse))
      return;

   if (UNLIKELY(in_panic()))
      return;

   task_info *curr = get_curr_task();
   arch_task_info_members *arch_fields = &curr->arch;
   void *buf = in_kernel ? fpu_kernel_regs : arch_fields->fpu_regs;

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

static void
fpu_no_coprocessor_fault_handler(regs *r)
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

   NOT_IMPLEMENTED();

   task_info *curr = get_curr_task();
   arch_task_info_members *arch_fields = &curr->arch;
   ASSERT(arch_fields->fpu_regs == NULL);

   if (x86_cpu_features.can_use_avx) {
      arch_fields->fpu_regs = kmalloc(CPU_XSAVE_AREA_SIZE);
   } else {
      arch_fields->fpu_regs = kmalloc(CPU_FXSAVE_AREA_SIZE);
   }

   VERIFY(arch_fields->fpu_regs); // TODO: handle this OOM case
}

static u32 fpu_context_count;

void fpu_context_begin(void)
{
   disable_preemption();

   if (++fpu_context_count == 1)
      fpu_enable();

   save_current_fpu_regs(true);
}

void fpu_context_end(void)
{
   restore_current_fpu_regs(true);

   /* Disable the FPU: any attempt to touch FPU registers triggers a fault. */

   ASSERT(fpu_context_count > 0);

   if (--fpu_context_count == 0)
      fpu_disable();

   enable_preemption();
}
