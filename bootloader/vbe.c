
#include <common/string_util.h>

#include "realmode_call.h"
#include "vbe.h"

void vga_set_video_mode(u8 mode)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   /*
    * ah = 0x0   => set video mode
    * al = mode  => mode [0 .. 0x13]
    */

   eax = mode;

   realmode_call(&realmode_int_10h, &eax, &ebx, &ecx, &edx, &esi, &edi);
}

void vbe_get_info_block(VbeInfoBlock *vb)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   bzero(vb, sizeof(*vb));

   eax = 0x4f00;
   edi = (u32) vb;

   memcpy(vb->VbeSignature, "VBE2", 4);
   realmode_call(&realmode_int_10h, &eax, &ebx, &ecx, &edx, &esi, &edi);

   if (eax != 0x004f)
      panic("VBE get info failed");
}

bool vbe_get_mode_info(u16 mode, ModeInfoBlock *mi)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   bzero(mi, sizeof(*mi));

   eax = 0x4f01;
   ecx = mode;
   edi = (u32) mi;

   realmode_call(&realmode_int_10h, &eax, &ebx, &ecx, &edx, &esi, &edi);

   if (eax != 0x004f)
      return false;

   return true;
}

bool vbe_set_video_mode(u16 mode)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   eax = 0x4f02;
   ebx = mode | VBE_SET_MODE_FLAG_USE_LINEAR_FB;

   realmode_call(&realmode_int_10h, &eax, &ebx, &ecx, &edx, &esi, &edi);

   if (eax != 0x004f)
      return false;

   return true;
}

bool vbe_get_current_mode(u16 *mode)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   eax = 0x4f03;

   realmode_call(&realmode_int_10h, &eax, &ebx, &ecx, &edx, &esi, &edi);

   if (eax != 0x004f)
      return false;

   *mode = ebx & 0xffff;
   return true;
}
