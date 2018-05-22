
#include <common/string_util.h>
#include "realmode_call.h"

void
realmode_call_asm(void *f, u32 *a, u32 *b, u32 *c, u32 *d, u32 *si, u32 *di);

void realmode_call(void *func,
                   u32 *eax_ref,
                   u32 *ebx_ref,
                   u32 *ecx_ref,
                   u32 *edx_ref,
                   u32 *esi_ref,
                   u32 *edi_ref)
{
   ASSERT(func != NULL);
   ASSERT(eax_ref != NULL);
   ASSERT(ebx_ref != NULL);
   ASSERT(ecx_ref != NULL);
   ASSERT(edx_ref != NULL);
   ASSERT(esi_ref != NULL);
   ASSERT(edi_ref != NULL);

   realmode_call_asm(func, eax_ref, ebx_ref,
                     ecx_ref, edx_ref, esi_ref, edi_ref);
}

void
realmode_call_by_val(void *func, u32 a, u32 b, u32 c, u32 d, u32 si, u32 di)
{
   ASSERT(func != NULL);
   realmode_call_asm(func, &a, &b, &c, &d, &si, &di);
}

void check_rm_out_regs(void)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   realmode_call(&realmode_test_out, &eax, &ebx, &ecx, &edx, &esi, &edi);

   printk("eax: %d\n", eax);
   printk("ebx: %d\n", ebx);
   printk("ecx: %d\n", ecx);
   printk("edx: %d\n", edx);
   printk("esi: %d\n", esi);
   printk("edi: %d\n", edi);
}
