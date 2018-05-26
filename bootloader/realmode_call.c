
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/arch/generic_x86/x86_utils.h>

#include "realmode_call.h"

extern u32 realmode_test_out;

void
realmode_call_asm(void *f, u32 *a, u32 *b,
                  u32 *c, u32 *d, u32 *si, u32 *di, u32 *flags_ref);

void realmode_call(void *func,
                   u32 *eax_ref,
                   u32 *ebx_ref,
                   u32 *ecx_ref,
                   u32 *edx_ref,
                   u32 *esi_ref,
                   u32 *edi_ref,
                   u32 *flags_ref)
{
   ASSERT(func != NULL);
   ASSERT(eax_ref != NULL);
   ASSERT(ebx_ref != NULL);
   ASSERT(ecx_ref != NULL);
   ASSERT(edx_ref != NULL);
   ASSERT(esi_ref != NULL);
   ASSERT(edi_ref != NULL);
   ASSERT(flags_ref != NULL);

   realmode_call_asm(func, eax_ref, ebx_ref, ecx_ref,
                     edx_ref, esi_ref, edi_ref, flags_ref);

   if (*eax_ref == 0xBAADCAFE && *ebx_ref == 0xCAFEBABE)
      panic("RM fault: 0x%x\n"
            "eip:      %p\n"
            "flags:    %p\n",
            *ecx_ref,
            (*edx_ref >> 16) * 16 + (*edx_ref & 0xffff),
            *esi_ref & 0xffff);
}

void
realmode_call_by_val(void *func, u32 a, u32 b, u32 c, u32 d, u32 si, u32 di)
{
   u32 flags_ignored;
   ASSERT(func != NULL);

   realmode_call(func, &a, &b, &c, &d, &si, &di, &flags_ignored);
}

void test_rm_call_working(void)
{
   u32 eax, ebx, ecx, edx, esi, edi, flags;

   realmode_call(&realmode_test_out, &eax, &ebx,
                 &ecx, &edx, &esi, &edi, &flags);

   ASSERT(eax == 2*1024*1024);
   ASSERT(ebx == 99);
   ASSERT(ecx == 100);
   ASSERT(edx == 102);
   ASSERT(esi == 300);
   ASSERT(edi == 350);
}

char bios_read_char(void)
{
   u32 eax, ebx, ecx, edx, esi, edi, flags;

   /*
    * ah = 0x0 => read key press
    */
   eax = 0;

   realmode_call(&realmode_int_16h, &eax, &ebx, &ecx, &edx, &esi, &edi, &flags);

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
   u32 eax, ebx, ecx, edx, esi, edi, flags;

   edx = drive;    /* DL = drive number */
   eax = 0x8 << 8; /* AH = read drive params */

   realmode_call(&realmode_int_13h, &eax, &ebx, &ecx, &edx, &esi, &edi, &flags);

   *heads_per_cylinder = ((edx >> 8) & 0xff) + 1; /* DH = MAX head num */

   /* sector count = lower 6 bits of CX (actual count, not MAX sector) */
   *sectors_per_track = ecx & 63;

   /* cyl is max cylinder number (therefore count + cyl + 1) */
   /* higher 8 bits of CX => lower 8 bits of cyl */
   /* bits 6, 7 of CX => bits 8, 9 of cyl (higher 2 bits) */
   *cylinder_count = 1 + (((ecx >> 8) & 0xff) | ((ecx & 192) << 2));

   if (flags & EFLAGS_CF)
      return false;

   return true;
}

extern u32 realmode_read_sectors;
extern u32 curr_sec;

u16 saved_cx;
u16 saved_dx;

void dump_chs(void)
{
   /*
    * the higher 8 bits of CX are the lower 8 bits of cyl
    * the bits 6, 7 of CX are the higher (8, 9) bits of cyl
    */
   u32 cyl = saved_cx >> 8 | ((saved_cx & 192) << 2);
   u32 head = saved_dx >> 8;
   u32 sec = saved_cx & 63; /* the lower 6 bits of CX */

   printk("CHS used: %u, %u, %u\n", cyl, head, sec);
}

void read_sectors(u32 dest_paddr, u32 lba_sector, u32 sector_count)
{
   u32 eax, ebx, ecx, edx, esi, edi, flags;

   eax = dest_paddr;
   ebx = lba_sector;
   ecx = lba_sector + sector_count - 1;

   realmode_call(&realmode_read_sectors,
                 &eax, &ebx, &ecx, &edx, &esi, &edi, &flags);

   if (eax) {
      printk("[ FAILED ]\n");
      printk("Read failed with last op err: %p\n", eax >> 8);
      printk("LBA sector: %u\n", curr_sec);
      dump_chs();
      panic("Unrecoverable read error");
   }
}
