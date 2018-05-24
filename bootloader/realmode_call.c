
#include <common/basic_defs.h>
#include "realmode_call.h"

extern u32 realmode_test_out;

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

char bios_read_char(void)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   /*
    * ah = 0x0 => read key press
    */
   eax = 0;

   realmode_call(&realmode_int_16h, &eax, &ebx, &ecx, &edx, &esi, &edi);

   /*
    * Return:
    *    ah => scan code of the key pressed down
    *    al => ASCII char
    */

   return eax & 0xFF; /* return just the ASCII char */
}


bool read_drive_params(u8 drive,
                       u32 *sectors_per_track,
                       u32 *heads_per_cylinder,
                       u32 *cylinder_count)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   edx = drive;    /* DL = drive number */
   eax = 0x8 << 8; /* AH = read drive params */

   realmode_call(&realmode_int_13h, &eax, &ebx, &ecx, &edx, &esi, &edi);

   *heads_per_cylinder = ((edx >> 8) & 0xff) + 1; /* DH = MAX head num */

   /* sector count = lower 6 bits of CX (actual count, not MAX sector) */
   *sectors_per_track = ecx & 63;

   /* cyl is max cylinder number (therefore count + cyl + 1) */
   /* higher 8 bits of CX => lower 8 bits of cyl */
   /* bits 6, 7 of CX => bits 8, 9 of cyl (higher 2 bits) */
   *cylinder_count = 1 + (((ecx >> 8) & 0xff) | ((ecx & 192) << 2));

   return true;
}
