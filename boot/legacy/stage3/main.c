/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/fat32_base.h>
#include <tilck/common/utils.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>
#include <tilck/common/simple_elf_loader.c.h>

#include <multiboot.h>

#include "basic_term.h"
#include "realmode_call.h"
#include "vbe.h"
#include "mm.h"
#include "common.h"

static mem_area_t *mem_areas = (void *) MEM_AREAS_BUF;
static u32 mem_areas_count;

bool graphics_mode; // false = text mode

u32 fb_paddr;
u32 fb_pitch;
u32 fb_width;
u32 fb_height;
u32 fb_bpp;

u8 fb_red_pos;
u8 fb_red_mask_size;
u8 fb_green_pos;
u8 fb_green_mask_size;
u8 fb_blue_pos;
u8 fb_blue_mask_size;

u16 selected_mode = VGA_COLOR_TEXT_MODE_80x25; /* default */

u8 current_device;
u32 sectors_per_track;
u32 heads_per_cylinder;
u32 cylinders_count;

static u32 ramdisk_max_size;
static u32 ramdisk_used_bytes;
static u32 ramdisk_first_data_sector;

static void calculate_ramdisk_fat_size(fat_header *hdr)
{
   const u32 sector_size = fat_get_sector_size(hdr);

   ramdisk_first_data_sector = fat_get_first_data_sector(hdr);
   ramdisk_max_size = fat_get_TotSec(hdr) * sector_size;
}

static void load_elf_kernel(const char *filepath, void **entry)
{
   fat_header *hdr = (fat_header *)RAMDISK_PADDR;
   void *free_space = (void *) (RAMDISK_PADDR + ramdisk_used_bytes);
   fat_entry *e;

   if (!(e = fat_search_entry(hdr, fat_get_type(hdr), filepath, NULL)))
      panic("Unable to open '%s'!\n", filepath);

   fat_read_whole_file(hdr, e, free_space, KERNEL_MAX_SIZE);

   Elf32_Ehdr *header = (Elf32_Ehdr *)free_space;

   VERIFY(header->e_ident[EI_MAG0] == ELFMAG0);
   VERIFY(header->e_ident[EI_MAG1] == ELFMAG1);
   VERIFY(header->e_ident[EI_MAG2] == ELFMAG2);
   VERIFY(header->e_ident[EI_MAG3] == ELFMAG3);
   VERIFY(header->e_ehsize == sizeof(*header));

   *entry = simple_elf_loader(header);
}

static multiboot_info_t *setup_multiboot_info(void)
{
   multiboot_info_t *mbi;
   multiboot_module_t *mod;

   mbi = (multiboot_info_t *) MBI_PADDR;
   bzero(mbi, sizeof(*mbi));

   mod = (multiboot_module_t *)(MBI_PADDR + sizeof(*mbi));
   bzero(mod, sizeof(*mod));

   mbi->flags |= MULTIBOOT_INFO_MEMORY;
   mbi->mem_lower = 0;
   mbi->mem_upper = 0;

   mbi->flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;

   if (!graphics_mode) {
      mbi->framebuffer_addr = 0xB8000;
      mbi->framebuffer_pitch = 80 * 2;
      mbi->framebuffer_width = 80;
      mbi->framebuffer_height = 25;
      mbi->framebuffer_bpp = 4;
      mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT;
   } else {
      mbi->framebuffer_addr = fb_paddr;
      mbi->framebuffer_pitch = fb_pitch;
      mbi->framebuffer_width = fb_width;
      mbi->framebuffer_height = fb_height;
      mbi->framebuffer_bpp = (u8)fb_bpp;
      mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
      mbi->framebuffer_red_field_position = fb_red_pos;
      mbi->framebuffer_red_mask_size = fb_red_mask_size;
      mbi->framebuffer_green_field_position = fb_green_pos;
      mbi->framebuffer_green_mask_size = fb_green_mask_size;
      mbi->framebuffer_blue_field_position = fb_blue_pos;
      mbi->framebuffer_blue_mask_size = fb_blue_mask_size;
   }

   mbi->flags |= MULTIBOOT_INFO_MODS;
   mbi->mods_addr = (u32)mod;
   mbi->mods_count = 1;
   mod->mod_start = RAMDISK_PADDR;

   /*
    * Pass via multiboot 'used bytes' as RAMDISK size instead of the real
    * RAMDISK size. This is useful if the kernel uses the RAMDISK read-only.
    */
   mod->mod_end = mod->mod_start + ramdisk_used_bytes;

   mbi->flags |= MULTIBOOT_INFO_MEM_MAP;

   multiboot_memory_map_t *mmmap =
      (void *)mbi->mods_addr + (mbi->mods_count * sizeof(multiboot_module_t));

   mbi->mmap_addr = (u32)mmmap;
   mbi->mmap_length = mem_areas_count * sizeof(multiboot_memory_map_t);

   for (u32 i = 0; i < mem_areas_count; i++) {

      mem_area_t *ma = mem_areas + i;

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

void bootloader_main(void)
{
   void *entry;
   multiboot_info_t *mbi;
   u32 ramdisk_used_sectors;

   vga_set_video_mode(VGA_COLOR_TEXT_MODE_80x25);
   init_bt();

   /* Sanity check: the variables in BSS should be zero-filled */
   ASSERT(!graphics_mode);
   ASSERT(!fb_paddr);

   printk("----- Hello from Tilck's legacy bootloader! -----\n\n");

   /* Sanity check: realmode_call should be able to return all reg values */
   test_rm_call_working();

   get_cpu_features();

   if (!x86_cpu_features.edx1.pse) {
      panic("Sorry, but your CPU is too old: no PSE (page size extension)");
   }

   mem_areas_count = read_memory_map(mem_areas);

#if BOOTLOADER_POISON_MEMORY
   poison_usable_memory(mem_areas, mem_areas_count);
#endif

   bool success =
      read_drive_params(current_device,
                        &sectors_per_track,
                        &heads_per_cylinder,
                        &cylinders_count);

   if (!success)
      panic("read_write_params failed");

   printk("Loading ramdisk... ");

   // Read FAT's header
   read_sectors(RAMDISK_PADDR, RAMDISK_SECTOR, 1 /* read just 1 sector */);

   calculate_ramdisk_fat_size((void *)RAMDISK_PADDR);

   // Now read all the meta-data up to the first data sector.
   read_sectors(RAMDISK_PADDR, RAMDISK_SECTOR, ramdisk_first_data_sector + 1);

   // Finally we're able to determine how big is the fatpart (pure data)
   ramdisk_used_bytes = fat_get_used_bytes((void *)RAMDISK_PADDR);

   ramdisk_used_sectors = (ramdisk_used_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
   read_sectors(RAMDISK_PADDR, RAMDISK_SECTOR, ramdisk_used_sectors);

   printk("[ OK ]\n");
   printk("Loading the ELF kernel... ");

   load_elf_kernel(KERNEL_FILE_PATH, &entry);

   printk("[ OK ]\n\n");

   ask_user_video_mode();

   while (!vbe_set_video_mode(selected_mode)) {
      printk("ERROR: unable to set the selected video mode!\n");
      printk("       vbe_set_video_mode(0x%x) failed.\n\n", selected_mode);
      printk("Please select a different video mode.\n\n");
      ask_user_video_mode();
   }

   mbi = setup_multiboot_info();

   /* Jump to the kernel */
   asmVolatile("jmp *%%ecx"
               : /* no output */
               : "a" (MULTIBOOT_BOOTLOADER_MAGIC),
                 "b" (mbi),
                 "c" (entry)
               : /* no clobber */);
}
