/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck_gen_headers/mod_console.h>
#include <tilck_gen_headers/mod_fb.h>
#include <tilck_gen_headers/krn_max_sz.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/fat32_base.h>
#include <tilck/common/utils.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>
#include <tilck/common/printk.h>
#include <tilck/common/color_defs.h>
#include <tilck/common/elf_types.h>
#include <tilck/common/simple_elf_loader.c.h>
#include <tilck/common/elf_calc_mem_size.c.h>

#include <multiboot.h>

#include "basic_term.h"
#include "realmode_call.h"
#include "vbe.h"
#include "mm.h"
#include "common.h"

#define LOADING_INITRD_STR            "Loading ramdisk... "
#define LOADING_BOOTPART_STR          "Loading bootpart... "

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

static struct mem_area ma_buf[64];

static u32
calc_fat_ramdisk_metadata_sz(struct fat_hdr *hdr)
{
   const u32 sector_size = fat_get_sector_size(hdr);
   const u32 first_data_sector = fat_get_first_data_sector(hdr);

   /* Note: the `first_data_sector` is *relative* to the start of the ramdisk */
   return sector_size * (first_data_sector + 1);
}

static void
load_elf_kernel(struct mem_info *mi,
                ulong ramdisk,
                ulong ramdisk_size,
                const char *filepath,
                void **entry)
{
   struct fat_hdr *hdr = (struct fat_hdr *)ramdisk;
   struct fat_entry *e;
   Elf32_Ehdr *header;
   ulong free_space;
   size_t len;

   if (!(e = fat_search_entry(hdr, fat_get_type(hdr), filepath, NULL)))
      panic("Unable to open '%s'!", filepath);

   free_space = get_usable_mem(mi, ramdisk + ramdisk_size, e->DIR_FileSize);

   if (!free_space)
      panic("No free space for kernel file after %p", ramdisk + ramdisk_size);

   len = fat_read_whole_file(hdr, e, (void *)free_space, e->DIR_FileSize);
   header = (Elf32_Ehdr *)free_space;

   VERIFY(len == e->DIR_FileSize);
   VERIFY(header->e_ident[EI_MAG0] == ELFMAG0);
   VERIFY(header->e_ident[EI_MAG1] == ELFMAG1);
   VERIFY(header->e_ident[EI_MAG2] == ELFMAG2);
   VERIFY(header->e_ident[EI_MAG3] == ELFMAG3);
   VERIFY(header->e_ehsize == sizeof(*header));

   *entry = simple_elf_loader(header);
}

static multiboot_info_t *
setup_multiboot_info(struct mem_info *mi,
                     ulong ramdisk_paddr,
                     ulong ramdisk_size)
{
   ulong free_mem;
   multiboot_info_t *mbi;
   multiboot_module_t *mod;

   /* Try first in the 1st 64 KB segment */
   free_mem = get_usable_mem(mi, 16 * KB, 48 * KB);

   if (!free_mem) {

      /* Second try in the 2nd 64 KB segment */
      free_mem = get_usable_mem(mi, 64 * KB, 48 * KB);

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
   mod->mod_start = ramdisk_paddr;
   mod->mod_end = mod->mod_start + ramdisk_size;

   mbi->flags |= MULTIBOOT_INFO_MEM_MAP;

   multiboot_memory_map_t *mmmap =
      (void *)mbi->mods_addr + (mbi->mods_count * sizeof(multiboot_module_t));

   mbi->mmap_addr = (u32)mmmap;
   mbi->mmap_length = mi->count * sizeof(multiboot_memory_map_t);

   for (u32 i = 0; i < mi->count; i++) {

      struct mem_area *ma = mi->mem_areas + i;

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

static void
dump_progress(const char *prefix_str, u32 curr, u32 tot)
{
   bt_movecur(bt_get_curr_row(), 0);
   printk("%s%u%% ", prefix_str, 100 * curr / tot);
}

static void
read_sectors_with_progress(const char *prefix_str,
                           u32 paddr, u32 first_sector, u32 count)
{
   const u32 chunk_sectors = 1024;
   const u32 chunks_count = count / chunk_sectors;
   const u32 rem = count - chunks_count * chunk_sectors;

   for (u32 chunk = 0; chunk < chunks_count; chunk++) {

      const u32 sectors_read = chunk * chunk_sectors;

      if (sectors_read > 0)
         dump_progress(prefix_str, sectors_read, count);

      read_sectors(paddr, first_sector, chunk_sectors);
      paddr += chunk_sectors * SECTOR_SIZE;
      first_sector += chunk_sectors;
   }

   if (rem > 0) {
      read_sectors(paddr, first_sector, rem);
   }

   dump_progress(prefix_str, count, count);
}

static void
write_ok_msg(void)
{
   bt_setcolor(COLOR_GREEN);
   printk("[ OK ]\n");
   bt_setcolor(DEFAULT_FG_COLOR);
}

static u32
do_ramdisk_compact_clusters(void *ramdisk, u32 rd_size)
{
   u32 ff_clu_off;         /* offset of ramdisk's first free cluster */

   ff_clu_off = fat_get_first_free_cluster_off(ramdisk);

   if (ff_clu_off < rd_size) {

      printk("Compacting ramdisk... ");

      fat_compact_clusters(ramdisk);
      ff_clu_off = fat_get_first_free_cluster_off(ramdisk);
      rd_size = fat_calculate_used_bytes(ramdisk);

      if (rd_size != ff_clu_off) {
         printk("\n");
         panic("fat_compact_clusters() failed: %u != %u", rd_size, ff_clu_off);
      }

      write_ok_msg();
   }

   return rd_size;
}

static void
load_fat_ramdisk(const char *load_str,
                 struct mem_info *mi,
                 u32 first_sec,
                 ulong min_paddr,
                 ulong *ref_rd_paddr,
                 u32 *ref_rd_size,
                 bool alloc_extra_page)
{
   u32 rd_sectors;         /* rd_size in 512-bytes sectors (rounded-up) */
   u32 rd_size;            /* ramdisk size (used bytes in the fat partition) */
   u32 rd_metadata_sz;     /* size of ramdisk's metadata, including the FATs */
   ulong rd_paddr;         /* ramdisk physical address */
   ulong free_mem;
   ulong size_to_alloc;

   printk("%s", load_str);
   free_mem = get_usable_mem_or_panic(mi, min_paddr, SECTOR_SIZE);

   // Read FAT's header
   read_sectors(free_mem, first_sec, 1 /* read just 1 sector */);

   rd_metadata_sz = calc_fat_ramdisk_metadata_sz((void *)free_mem);

   free_mem =
      get_usable_mem_or_panic(mi,
                              min_paddr,
                              rd_metadata_sz);

   // Now read all the meta-data up to the first data sector.
   read_sectors(free_mem, first_sec, rd_metadata_sz / SECTOR_SIZE);

   // Finally we're able to determine how big is the fatpart (pure data)
   rd_size = fat_calculate_used_bytes((void *)free_mem);

   /* Calculate rd_size in sectors, rounding up at SECTOR_SIZE */
   rd_sectors = (rd_size + SECTOR_SIZE - 1) / SECTOR_SIZE;

   size_to_alloc = SECTOR_SIZE * rd_sectors;

   if (alloc_extra_page)
      size_to_alloc += PAGE_SIZE;

   free_mem = get_usable_mem(mi, min_paddr, size_to_alloc);

   if (!free_mem) {
      panic("Unable to allocate %u KB after %p for the ramdisk",
            SECTOR_SIZE * rd_sectors / KB, min_paddr);
   }

   rd_paddr = free_mem;
   read_sectors_with_progress(load_str,
                              rd_paddr,
                              first_sec,
                              rd_sectors);

   bt_movecur(bt_get_curr_row(), 0);
   printk("%s", load_str);
   write_ok_msg();

   /* Return ramdisk's paddr and size using the OUT parameters */
   *ref_rd_paddr = rd_paddr;
   *ref_rd_size = rd_size;
}

void bootloader_main(void)
{
   multiboot_info_t *mbi;
   ulong rd_paddr;         /* initrd's physical address */
   u32 rd_size;            /* initrd's size (used bytes in the fat partition) */
   ulong bp_paddr;         /* bootpart's physical address */
   u32 bp_size;            /* bootpart's size (used bytes, as rd_size) */
   ulong bp_min_paddr;
   void *entry;
   bool success;
   struct mem_info mi;

   vga_set_video_mode(VGA_COLOR_TEXT_MODE_80x25);
   init_bt();

   /* Sanity check: the variables in BSS should be zero-filled */
   ASSERT(!graphics_mode);
   ASSERT(!fb_paddr);

   bt_setcolor(COLOR_BRIGHT_WHITE);
   printk("----- Hello from Tilck's legacy bootloader! -----\n\n");
   bt_setcolor(DEFAULT_FG_COLOR);

   /* Sanity check: realmode_call should be able to return all reg values */
   test_rm_call_working();

   get_cpu_features();

   if (!x86_cpu_features.edx1.pse)
      panic("Sorry, but your CPU is too old: no PSE (page size extension)");

   read_memory_map(ma_buf, sizeof(ma_buf), &mi);

   if (BOOTLOADER_POISON_MEMORY)
      poison_usable_memory(&mi);

   success =
      read_drive_params(current_device,
                        &sectors_per_track,
                        &heads_per_cylinder,
                        &cylinders_count);

   if (!success)
      panic("read_write_params failed");

   /* Load the INITRD */
   load_fat_ramdisk(LOADING_INITRD_STR,
                    &mi,
                    INITRD_SECTOR,
                    KERNEL_PADDR + KERNEL_MAX_SIZE,
                    &rd_paddr,
                    &rd_size,
                    true);       /* alloc_extra_page */

   /* Set bootpart's lowest paddr, leaving some safe margin */
   bp_min_paddr = rd_paddr + rd_size + 2 * PAGE_SIZE;

   /* Round-up bootpart's lowest paddr at page boundary */
   bp_min_paddr = round_up_at(bp_min_paddr, PAGE_SIZE);

   /* Load the BOOTPART from which we'll load the kernel */
   load_fat_ramdisk(LOADING_BOOTPART_STR,
                    &mi,
                    BOOTPART_SEC,
                    bp_min_paddr,
                    &bp_paddr,
                    &bp_size,
                    false);       /* alloc_extra_page */

   /* Compact initrd's clusters, if necessary */
   rd_size = do_ramdisk_compact_clusters((void *)rd_paddr, rd_size);

   /*
    * Increase rd_size by 1 page in order to allow Tilck's kernel to
    * align the first data sector, if necessary.
    */
   rd_size += 4 * KB;

   printk("Loading the ELF kernel... ");
   load_elf_kernel(&mi, bp_paddr, bp_size, KERNEL_FILE_PATH, &entry);
   write_ok_msg();
   printk("\n");

   if (!MOD_console) {

      printk("WARNING: MOD_console=0, Tilck won't support any type of video\n");
      printk("console. Use the serial console instead.\n\n");

      printk("Press ANY key to boot");
      bios_read_char();

      init_bt();
      printk("<No video console>");

   } else if (!MOD_fb) {

      if (BOOT_ASK_VIDEO_MODE) {
         printk("WARNING: MOD_fb=0, Tilck won't support graphics mode.\n");
         printk("Force-selecting text-mode.\n\n");
         printk("Press ANY key to boot");
         bios_read_char();
      }

   } else {

      ask_user_video_mode(&mi);

      while (!vbe_set_video_mode(selected_mode)) {
         printk("ERROR: unable to set the selected video mode!\n");
         printk("       vbe_set_video_mode(0x%x) failed.\n\n", selected_mode);
         printk("Please select a different video mode.\n\n");
         ask_user_video_mode(&mi);
      }
   }

   mbi = setup_multiboot_info(&mi, rd_paddr, rd_size);

   /* Jump to the kernel */
   asmVolatile("jmp *%%ecx"
               : /* no output */
               : "a" (MULTIBOOT_BOOTLOADER_MAGIC),
                 "b" (mbi),
                 "c" (entry)
               : /* no clobber */);
}
