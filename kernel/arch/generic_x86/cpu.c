
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>
#include <exos/common/utils.h>
#include <exos/common/arch/generic_x86/cpu_features.h>

#include <exos/kernel/process.h>
#include <exos/kernel/fault_resumable.h>

extern const char *x86_exception_names[32];

void asm_enable_osxsave(void);
void asm_enable_sse(void);
void asm_enable_avx(void);

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
   write_cr0(read_cr0() | CR0_TS);
}

static u32 fpu_context_count;

void fpu_context_begin(void)
{
   disable_preemption();

   if (++fpu_context_count == 1)
      write_cr0(read_cr0() & ~CR0_TS);

   // TODO: use FXSAVE [SSE] or XSAVE [AVX] to save the FPU regs.
}

void fpu_context_end(void)
{
   // TODO: use FXRSTOR [SSE] or XRSTOR [AVX] to restore the FPU regs.

   /* Disable the FPU: any attempt to touch FPU registers triggers a fault. */

   ASSERT(fpu_context_count > 0);

   if (--fpu_context_count == 0)
      write_cr0(read_cr0() | CR0_TS);

   enable_preemption();
}
