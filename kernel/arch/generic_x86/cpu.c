
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/utils.h>
#include <common/arch/generic_x86/cpu_features.h>

#include <exos/fault_resumable.h>

void asm_enable_osxsave(void);
void asm_enable_sse(void);
void asm_enable_avx(void);

extern const char *exception_messages[32];

void enable_cpu_features(void)
{
   if (x86_cpu_features.edx1.sse && x86_cpu_features.edx1.fxsr) {

      asm_enable_sse();
      x86_cpu_features.can_use_sse = true;

      if (x86_cpu_features.edx1.sse2)
         x86_cpu_features.can_use_sse2 = true;

      printk("[CPU features] SSE enabled\n");


      if (x86_cpu_features.ecx1.osxsave) {

         u32 res = fault_resumable_call(~0, &asm_enable_osxsave, 0);

         if (res) {

            u32 n = get_first_set_bit_index(res);

            printk("[CPU features] Enable OSXSAVE failed: %i [%s]\n",
                   n, exception_messages[n]);

            return;
         }

         printk("[CPU features] OSXSAVE enabled\n");

         if (x86_cpu_features.ecx1.avx) {

            asm_enable_avx();
            x86_cpu_features.can_use_avx = true;

            if (x86_cpu_features.avx2)
               x86_cpu_features.can_use_avx2 = true;

            printk("[CPU features] AVX enabled\n");
         }
      }
   }
}
