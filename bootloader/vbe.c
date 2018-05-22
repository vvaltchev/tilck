
#include "realmode_call.h"
#include "vbe.h"

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
