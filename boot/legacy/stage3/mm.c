/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>

#include <multiboot.h>

#include "realmode_call.h"
#include "mm.h"

#define BIOS_INT15h_READ_MEMORY_MAP        0xE820
#define BIOS_INT15h_READ_MEMORY_MAP_MAGIC  0x534D4150

void read_memory_map(void *buf, size_t buf_size, mem_info *mi)
{
   typedef struct PACKED {

      u32 base_low;
      u32 base_hi;
      u32 len_low;
      u32 len_hi;
      u32 type;
      u32 acpi;

   } bios_mem_area_t;

   STATIC_ASSERT(sizeof(bios_mem_area_t) <= sizeof(mem_area_t));

   u32 eax, ebx, ecx, edx, esi, edi, flags;
   mem_area_t *mem_areas = buf;
   uptr buf_end = (uptr) buf + buf_size;
   u32 mem_areas_count = 0;

   bios_mem_area_t *bios_mem_area = ((void *) (mem_areas - 1));
   bzero(bios_mem_area, sizeof(bios_mem_area_t));

   /* es = 0 */
   edi = (u32)bios_mem_area;
   ebx = 0;

   while (true) {

      mem_areas->acpi = 1;
      eax = BIOS_INT15h_READ_MEMORY_MAP;
      edx = BIOS_INT15h_READ_MEMORY_MAP_MAGIC;
      ecx = sizeof(bios_mem_area_t);

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

      mem_area_t m = {
         .base = bios_mem_area->base_low | ((u64)bios_mem_area->base_hi << 32),
         .len  = bios_mem_area->len_low  | ((u64)bios_mem_area->len_hi << 32),
         .type = bios_mem_area->type,
         .acpi = bios_mem_area->acpi,
      };

      if ((uptr)(mem_areas + mem_areas_count + sizeof(mem_area_t)) > buf_end)
         panic("No enough memory for the memory map");

      if ((uptr)(mem_areas + mem_areas_count) == (uptr)bios_mem_area)
         panic("No enough memory for the memory map");

      memcpy(mem_areas + mem_areas_count, &m, sizeof(mem_area_t));
      mem_areas_count++;
   }

   mi->mem_areas = mem_areas;
   mi->count = mem_areas_count;
}

void poison_usable_memory(mem_info *mi)
{
   for (u32 i = 0; i < mi->count; i++) {

      mem_area_t *ma = mi->mem_areas + i;

      if (ma->type == MEM_USABLE && ma->base >= MB) {

         /* Poison only memory regions above the 1st MB */

         memset32(TO_PTR(ma->base),
                  KMALLOC_FREE_MEM_POISON_VAL,
                  (u32)ma->len / 4);
      }
   }
}

uptr get_usable_mem(mem_info *mi, uptr min_paddr, uptr size)
{
   for (u32 i = 0; i < mi->count; i++) {

      mem_area_t *ma = mi->mem_areas + i;
      uptr mbase = ma->base;
      uptr mend = ma->base + ma->len;

      if (ma->type != MEM_USABLE)
         continue;

      if (mend <= min_paddr)
         continue;

      if (mbase < min_paddr) {
         /*
          * The memory area starts before our the min address we can use,
          * therefore, for our purposes it's as if it just started at min_addr.
          */
         mbase = min_paddr;
      }

      if ((mend - mbase) >= size) {

         /* Great, we have enough space in this area. */
         return mbase;
      }
   }

   return 0;
}

uptr get_usable_mem_or_panic(mem_info *mi, uptr min_paddr, uptr size)
{
   uptr free_mem = get_usable_mem(mi, min_paddr, size);

   if (!free_mem)
      panic("Unable to allocate %u bytes after %p", size, min_paddr);

   return free_mem;
}
