
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

   ASSERT(eax == 23);
   ASSERT(ebx == 99);
   ASSERT(ecx == 100);
   ASSERT(edx == 102);
   ASSERT(esi == 300);
   ASSERT(edi == 350);
}

void bios_get_vbe_info_block(VbeInfoBlock *vb)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   eax = 0x4f00;
   edi = (u32) vb;

   realmode_call(&realmode_int_10h, &eax, &ebx, &ecx, &edx, &esi, &edi);

   if (eax != 0x004f)
      panic("VBE get info failed");
}

bool bios_get_vbe_info_mode(u16 mode, ModeInfoBlock *mi)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   eax = 0x4f01;
   ecx = mode;
   edi = (u32) mi;

   realmode_call(&realmode_int_10h, &eax, &ebx, &ecx, &edx, &esi, &edi);

   if (eax != 0x004f)
      return false;

   return true;
}
