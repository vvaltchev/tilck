
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/utils.h>
#include <common/arch/generic_x86/cpu_features.h>

#include <exos/fault_resumable.h>

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
         return;
   }

   if (x86_cpu_features.ecx1.xsave) {

      if (!enable_osxsave())
         return;

      if (x86_cpu_features.ecx1.avx)
         if (!enable_avx())
            return;
   }
}
