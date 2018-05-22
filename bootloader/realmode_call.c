
#include <common/basic_defs.h>
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

extern u32 realmode_test_out;

void test_rm_call_working(void)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   realmode_call(&realmode_test_out, &eax, &ebx, &ecx, &edx, &esi, &edi);

   ASSERT(eax == 23);
   ASSERT(ebx == 99);
   ASSERT(ecx == 100);
   ASSERT(edx == 102);
   ASSERT(esi == 300);
   ASSERT(edi == 350);
}

