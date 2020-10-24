/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include "common.h"
#include "mm.h"
#include "vbe.h"

multiboot_info_t *
setup_multiboot_info(ulong ramdisk_paddr, ulong ramdisk_size)
{
   ulong free_mem;
   multiboot_info_t *mbi;
   multiboot_module_t *mod;

   /* Try first in the 1st 64 KB segment */
   free_mem = get_usable_mem(&g_meminfo, 16 * KB, 48 * KB);

   if (!free_mem) {

      /* Second try in the 2nd 64 KB segment */
      free_mem = get_usable_mem(&g_meminfo, 64 * KB, 48 * KB);

      if (!free_mem)
         panic("Unable to allocate memory for the multiboot info");
   }

   mbi = (multiboot_info_t *) free_mem;
   bzero(mbi, sizeof(*mbi));

   mod = (multiboot_module_t *)((char*)mbi + sizeof(*mbi));
   bzero(mod, sizeof(*mod));

   mbi->flags |= MULTIBOOT_INFO_MEMORY;
   mbi->mem_lower = 0;
   mbi->mem_upper = 0;

   mbi->flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;

   if (selected_mode == VGA_COLOR_TEXT_MODE_80x25) {

      mbi->framebuffer_addr = 0xB8000;
      mbi->framebuffer_pitch = 80 * 2;
      mbi->framebuffer_width = 80;
      mbi->framebuffer_height = 25;
      mbi->framebuffer_bpp = 4;
      mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT;

   } else {

      struct ModeInfoBlock *mi = usable_vbe_mode_info_block;

      if (!vbe_get_mode_info(selected_mode, mi))
         panic("vbe_get_mode_info(0x%x) failed", selected_mode);

      mbi->framebuffer_addr = mi->PhysBasePtr;
      mbi->framebuffer_pitch = mi->BytesPerScanLine;
      mbi->framebuffer_width = mi->XResolution;
      mbi->framebuffer_height = mi->YResolution;
      mbi->framebuffer_bpp = mi->BitsPerPixel;
      mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
      mbi->framebuffer_red_field_position = mi->RedFieldPosition;
      mbi->framebuffer_red_mask_size = mi->RedMaskSize;
      mbi->framebuffer_green_field_position = mi->GreenFieldPosition;
      mbi->framebuffer_green_mask_size = mi->GreenMaskSize;
      mbi->framebuffer_blue_field_position = mi->BlueFieldPosition;
      mbi->framebuffer_blue_mask_size = mi->BlueMaskSize;

      if (vbe_info_block->VbeVersion >= 0x300)
         mbi->framebuffer_pitch = mi->LinBytesPerScanLine;
   }

   mbi->flags |= MULTIBOOT_INFO_MODS;
   mbi->mods_addr = (u32)mod;
   mbi->mods_count = 1;
   mod->mod_start = ramdisk_paddr;
   mod->mod_end = mod->mod_start + ramdisk_size;

   mbi->flags |= MULTIBOOT_INFO_MEM_MAP;

   multiboot_memory_map_t *mmmap =
      (void *)mbi->mods_addr + (mbi->mods_count * sizeof(multiboot_module_t));

   mbi->mmap_addr = (u32)mmmap;
   mbi->mmap_length = g_meminfo.count * sizeof(multiboot_memory_map_t);

   for (u32 i = 0; i < g_meminfo.count; i++) {

      struct mem_area *ma = g_meminfo.mem_areas + i;

      if (ma->type == MEM_USABLE) {
         if (ma->base < mbi->mem_lower * KB)
            mbi->mem_lower = (u32)(ma->base / KB);

         if (ma->base + ma->len > mbi->mem_upper * KB)
            mbi->mem_upper = (u32)((ma->base + ma->len) / KB);
      }

      mmmap[i] = (multiboot_memory_map_t) {
         .size = sizeof(multiboot_memory_map_t) - sizeof(u32),
         .addr = ma->base,
         .len = ma->len,
         .type = bios_to_multiboot_mem_region(ma->type),
      };
   }

   return mbi;
}
