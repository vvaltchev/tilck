
#include <common/basic_defs.h>
#include <common/arch/generic_x86/cpu_features.h>

void asm_enable_sse(void);

void enable_cpu_features(void)
{
   get_x86_cpu_features();

   if (x86_cpu_features.edx1.sse && x86_cpu_features.edx1.fxsr) {
      asm_enable_sse();
   }
}
