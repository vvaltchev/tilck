
#include <common/basic_defs.h>
#include <common/arch/generic_x86/cpu_features.h>

void asm_enable_sse(void);
void asm_enable_avx(void);

void enable_cpu_features(void)
{
   get_x86_cpu_features();

   if (x86_cpu_features.edx1.sse && x86_cpu_features.edx1.fxsr) {

      asm_enable_sse();

      if (x86_cpu_features.avx2) {
         // This does not work for some reason yet.
         // asm_enable_avx();
      }
   }
}
