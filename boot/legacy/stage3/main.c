/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck_gen_headers/mod_console.h>
#include <tilck_gen_headers/mod_fb.h>
#include <tilck_gen_headers/krn_max_sz.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/fat32_base.h>
#include <tilck/common/utils.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>
#include <tilck/common/printk.h>
#include <tilck/common/color_defs.h>
#include <tilck/common/elf_types.h>
#include <tilck/common/elf_calc_mem_size.c.h>

#include <multiboot.h>

#include "basic_term.h"
#include "realmode_call.h"
#include "vbe.h"
#include "mm.h"
#include "common.h"

#define LOADING_INITRD_STR            "Loading ramdisk... "
#define LOADING_BOOTPART_STR          "Loading bootpart... "

video_mode_t selected_mode = VGA_COLOR_TEXT_MODE_80x25;

u8 current_device;
u32 sectors_per_track;
u32 heads_per_cylinder;
u32 cylinders_count;

ulong bp_paddr;         /* bootpart's physical address */
u32 bp_size;            /* bootpart's size (used bytes, as rd_size) */
void *loaded_kernel_file;

static struct mem_area ma_buf[64];
struct mem_info g_meminfo;

bool
load_kernel_file(ulong ramdisk,
                 ulong ramdisk_size,
                 const char *filepath)
{
   struct fat_hdr *hdr = (struct fat_hdr *)ramdisk;
   struct fat_entry *e;
   Elf32_Ehdr *header;
   ulong free_space;
   size_t len;

   if (!(e = fat_search_entry(hdr, fat_get_type(hdr), filepath, NULL)))
      panic("Unable to open '%s'!", filepath);

   free_space =
      get_usable_mem(&g_meminfo, ramdisk + ramdisk_size, e->DIR_FileSize);

   if (!free_space)
      panic("No free space for kernel file after %p", ramdisk + ramdisk_size);

   loaded_kernel_file = NULL;
   len = fat_read_whole_file(hdr, e, (void *)free_space, e->DIR_FileSize);
   header = (Elf32_Ehdr *)free_space;

   if (len != e->DIR_FileSize              ||
       header->e_ident[EI_MAG0] != ELFMAG0 ||
       header->e_ident[EI_MAG1] != ELFMAG1 ||
       header->e_ident[EI_MAG2] != ELFMAG2 ||
       header->e_ident[EI_MAG3] != ELFMAG3 ||
       header->e_ehsize != sizeof(*header))
   {
      return false;
   }

   loaded_kernel_file = header;
   return true;
}

void bootloader_main(void)
{
   multiboot_info_t *mbi;
   ulong rd_paddr;         /* initrd's physical address */
   u32 rd_size;            /* initrd's size (used bytes in the fat partition) */
   ulong bp_min_paddr;
   void *entry;
   bool success;

   init_common_bootloader_code(&legacy_boot_intf);
   vga_set_video_mode(VGA_COLOR_TEXT_MODE_80x25);
   init_bt();

   /* Sanity check: the variables in BSS should be zero-filled */
   ASSERT(!sectors_per_track);
   ASSERT(!heads_per_cylinder);
   ASSERT(!cylinders_count);

   bt_setcolor(COLOR_BRIGHT_WHITE);
   printk("----- Hello from Tilck's legacy bootloader! -----\n\n");
   bt_setcolor(DEFAULT_FG_COLOR);

   /* Sanity check: realmode_call should be able to return all reg values */
   test_rm_call_working();

   get_cpu_features();

   if (!x86_cpu_features.edx1.pse)
      panic("Sorry, but your CPU is too old: no PSE (page size extension)");

   read_memory_map(ma_buf, sizeof(ma_buf), &g_meminfo);

   if (BOOTLOADER_POISON_MEMORY)
      poison_usable_memory(&g_meminfo);

   success =
      read_drive_params(current_device,
                        &sectors_per_track,
                        &heads_per_cylinder,
                        &cylinders_count);

   if (!success)
      panic("read_write_params failed");

   /* Load the INITRD */
   load_fat_ramdisk(LOADING_INITRD_STR,
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

   success = common_bootloader_logic();

   if (!success)
      panic("Boot aborted");

   entry = simple_elf_loader(loaded_kernel_file);
   mbi = setup_multiboot_info(rd_paddr, rd_size);

   /* Jump to the kernel */
   asmVolatile("jmp *%%ecx"
               : /* no output */
               : "a" (MULTIBOOT_BOOTLOADER_MAGIC),
                 "b" (mbi),
                 "c" (entry)
               : /* no clobber */);
}
