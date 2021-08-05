/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>

#include <multiboot.h>

#include "realmode_call.h"
#include "common.h"
#include "mm.h"

#define BIOS_INT15h_READ_MEMORY_MAP        0xE820
#define BIOS_INT15h_READ_MEMORY_MAP_MAGIC  0x534D4150

static u64
calculate_tot_usable_mem(struct mem_info *mi)
{
   u64 tot_usable = 0;

   for (u32 i = 0; i < mi->count; i++) {

      if (mi->mem_areas[i].type == MEM_USABLE)
         tot_usable += mi->mem_areas[i].len;
   }

   return tot_usable;
}

static void
e820_mmap(void *buf, size_t buf_size, struct mem_info *mi)
{
   struct PACKED {

      u32 base_low;
      u32 base_hi;
      u32 len_low;
      u32 len_hi;
      u32 type;
      u32 acpi;

   } *bios_mem_area = (void *)BIOS_MEM_AREA_BUF;

   u32 eax, ebx, ecx, edx, esi, edi, flags;
   struct mem_area *mem_areas = buf;
   ulong buf_end = (ulong) buf + buf_size;
   u32 mem_areas_count = 0;

   /*
    * NOTE: ES = 0. This means that `bios_mem_area` MUST BE a pointer in the
    * first 64 KB. In order to support pointers in the 1st MB, realmode_call()
    * has to be extended to support passing a value for the ES segment register
    * as well.
    */
   edi = (u32)bios_mem_area;
   ebx = 0;

   while (true) {

      mem_areas->acpi = 1;
      eax = BIOS_INT15h_READ_MEMORY_MAP;
      edx = BIOS_INT15h_READ_MEMORY_MAP_MAGIC;
      ecx = sizeof(*bios_mem_area);

      realmode_call(&realmode_int_15h, &eax, &ebx,
                    &ecx, &edx, &esi, &edi, &flags);

      if (!ebx)
         break;

      if (flags & EFLAGS_CF) {

         if (mem_areas_count > 0)
            break;

         panic("Error while reading memory map: CF set");
      }

      if (eax != BIOS_INT15h_READ_MEMORY_MAP_MAGIC)
         panic("Error while reading memory map: eax != magic");

      struct mem_area m = {
         .base = bios_mem_area->base_low | ((u64)bios_mem_area->base_hi << 32),
         .len  = bios_mem_area->len_low  | ((u64)bios_mem_area->len_hi << 32),
         .type = bios_mem_area->type,
         .acpi = bios_mem_area->acpi,
      };

      if (m.base == 0 && m.len == 0) {

         /*
          * Hack: on some emulators, like PCem, while emulating an Intel VC440FX
          * machine, E820 seems to exist and behave well, but in reality it
          * doesn't report EBX=0 as "end", nor it sets the carry in EFLAGS.
          * This might be caused by a corrupted firmware. This needs testing on
          * real hardware.
          */
         break;
      }

      if ((ulong)(mem_areas + mem_areas_count) >= buf_end)
         panic("No enough memory for the memory map");

      memcpy(mem_areas + mem_areas_count, &m, sizeof(struct mem_area));
      mem_areas_count++;
   }

   mi->mem_areas = mem_areas;
   mi->count = mem_areas_count;
}

static void
legacy_88h_mmap(void *buf, size_t buf_size, struct mem_info *mi)
{
   /*
    * INT 15h, AH=88h - Get Extended Memory Size
    * For more, see: http://www.uruk.org/orig-grub/mem64mb.html
    */

   u32 eax = 0x88;
   u32 ebx = 0;
   u32 ecx = 0;
   u32 edx = 0;
   u32 esi = 0;
   u32 edi = 0;
   u32 flags = 0;

   realmode_call(&realmode_int_15h, &eax, &ebx,
                 &ecx, &edx, &esi, &edi, &flags);

   if (flags & EFLAGS_CF)
      panic("INT 15h, AH=88h failed");

   /*
    * Now in AX we should get the number of contiguous KB above 1 MB.
    * For the memory below 1 MB, we can use a conservative hard-coded
    * configuration.
    */

   mi->mem_areas = buf;

   mi->mem_areas[0].base = 0x500;
   mi->mem_areas[0].len = 0x80000 - 0x500;
   mi->mem_areas[0].type = MEM_USABLE;

   mi->mem_areas[1].base = 1 * MB; // + 1 MB
   mi->mem_areas[1].len = (eax & 0xffff) * KB;
   mi->mem_areas[1].type = MEM_USABLE;

   mi->count = 2;
}

void read_memory_map(void *buf, size_t buf_size, struct mem_info *mi)
{
   /* Try first with the most reliable method for machines made after 2002 */
   e820_mmap(buf, buf_size, mi);

   if (calculate_tot_usable_mem(mi) >= 1 * MB) {

      /*
       * E820 succeeded.
       *
       * Why checking for success in this weird way? On older machines, at
       * least on emulators like PCem, E820 might appear to work, but reporting
       * only 640 KB of available memory, despite the VM having more than 16 MB.
       * So, to detect such weird cases, we just check if E820 detected more
       * than 1 MB of usable memory. If it didn't, something went seriously
       * wrong.
       *
       * Q: What if E820 legitimately returned a memory map with less than 1 MB
       * of usable memory?
       *
       * A: Tilck cannot boot nor run in any case on x86 with less than 1 MB of
       * memory. On other architectures, it might be able to, but it won't use
       * this x86-specific bootloader.
       */

      return;
   }

   /* E820 explicitly failed or didn't work properly */
   bzero(buf, buf_size);
   mi->count = 0;

   /* Try with INT 15h, AX=88h, which should work on any PC */
   legacy_88h_mmap(buf, buf_size, mi);
}

void poison_usable_memory(struct mem_info *mi)
{
   if (!in_hypervisor())
      return; /* it's better to use this feature only in VMs */

   for (u32 i = 0; i < mi->count; i++) {

      struct mem_area *ma = mi->mem_areas + i;

      if (ma->type == MEM_USABLE && ma->base >= MB) {

         /* Poison only memory regions above the 1st MB */
         memset32(TO_PTR(ma->base), FREE_MEM_POISON_VAL, (u32)ma->len / 4);
      }
   }
}

ulong get_usable_mem(struct mem_info *mi, ulong min_paddr, ulong size)
{
   struct mem_area *ma;
   u64 mbase, mend;

   for (u32 i = 0; i < mi->count; i++) {

      ma = mi->mem_areas + i;
      mbase = ma->base;
      mend = ma->base + ma->len;

      if (ma->type != MEM_USABLE)
         continue;

      if (mend <= min_paddr)
         continue;

      if (mbase < min_paddr) {
         /*
          * The memory area starts before the lowest address we can use,
          * therefore, for our purposes it's as if it just started at min_addr.
          */
         mbase = min_paddr;
      }

      if (mbase + size >= (1ull << 32))
         continue;

      if (mend - mbase >= size) {

         /* Great, we have enough space in this area. */
         return mbase;
      }
   }

   return 0;
}

ulong get_high_usable_mem(struct mem_info *mi, ulong size)
{
   struct mem_area *ma;
   u64 mbase, mend;
   u64 candidate = 0;

   for (u32 i = 0; i < mi->count; i++) {

      ma = mi->mem_areas + i;
      mbase = ma->base;
      mend = ma->base + ma->len;

      if (ma->type != MEM_USABLE)
         continue;

      if (ma->len < size)
         continue;

      if (mend - size >= (1ull << 32)) {

         if ((1ull << 32) - size >= mbase) {

            /*
             * This is, by definition, the highest possible addr in the 32-bit
             * address space. Since it belongs to this free mem area, it's the
             * ultimate candidate addr.
             */

            candidate = (1ull << 32) - size;
            break;
         }

         /*
          * The mem area goes beyond the 32-bit limit, but it cannot contain
          * a chunk of our size within the 32-bit limit.
          */
         continue;
      }

      /* If the end of this area is higher than the candidate, take it */
      if (mend - size > candidate)
         candidate = mend - size;
   }

   return (ulong)candidate;
}
