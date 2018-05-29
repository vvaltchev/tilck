
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/utils.h>
#include <common/arch/generic_x86/cpu_features.h>

#include <exos/fault_resumable.h>

extern const char *exception_messages[32];

void asm_enable_osxsave(void);
void asm_enable_sse(void);
void asm_enable_avx(void);

static bool enable_sse(void)
{
   u32 res = fault_resumable_call(~0, &asm_enable_sse, 0);

   if (res) {

      u32 n = get_first_set_bit_index(res);

      printk("Enable SSE failed: fault %i [%s]\n",
             n, exception_messages[n]);

      return false;
   }

   x86_cpu_features.can_use_sse = true;

   if (x86_cpu_features.edx1.sse2)
      x86_cpu_features.can_use_sse2 = true;

   printk("[CPU features] SSE enabled\n");
   return true;
}

static bool enable_osxsave(void)
{
   u32 res = fault_resumable_call(~0, &asm_enable_osxsave, 0);

   if (res) {

      u32 n = get_first_set_bit_index(res);

      printk("Enable OSXSAVE failed: fault %i [%s]\n",
             n, exception_messages[n]);

      return false;
   }

   printk("[CPU features] OSXSAVE enabled\n");
   return true;
}

static bool enable_avx(void)
{
   u32 res = fault_resumable_call(~0, &asm_enable_avx, 0);

   if (res) {

      u32 n = get_first_set_bit_index(res);

      printk("Enable AVX failed: fault %i [%s]\n",
             n, exception_messages[n]);

      return false;
   }

   x86_cpu_features.can_use_avx = true;

   if (x86_cpu_features.avx2)
      x86_cpu_features.can_use_avx2 = true;

   printk("[CPU features] AVX enabled\n");
   return true;
}

void enable_cpu_features(void)
{
   if (x86_cpu_features.edx1.sse && x86_cpu_features.edx1.fxsr) {
      if (!enable_sse())
         return;
   }

   if (x86_cpu_features.ecx1.osxsave) {

      if (!enable_osxsave())
         return;

      if (x86_cpu_features.ecx1.avx)
         if (!enable_avx())
            return;
   }
}
