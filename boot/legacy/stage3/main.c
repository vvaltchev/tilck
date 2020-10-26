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

ulong initrd_paddr;    /* initrd's physical address */
u32 initrd_size;       /* initrd's size (used bytes in the fat partition) */
ulong bp_paddr;        /* bootpart's physical address */
u32 bp_size;           /* bootpart's size (used bytes in the fat partition) */

static struct mem_area ma_buf[64];
struct mem_info g_meminfo;

bool
load_kernel_file(ulong ramdisk,
                 ulong ramdisk_size,
                 const char *filepath,
                 void **file_paddr)
{
   struct fat_hdr *hdr = (struct fat_hdr *)ramdisk;
   struct fat_entry *e;
   ulong free_space;
   size_t len;

   if (!(e = fat_search_entry(hdr, fat_get_type(hdr), filepath, NULL))) {
      printk("ERROR: Unable to open '%s'\n", filepath);
      return false;
   }

   free_space =
      get_usable_mem(&g_meminfo, ramdisk + ramdisk_size, e->DIR_FileSize);

   if (!free_space) {

      printk("ERROR: No free space for kernel file at %p\n",
             TO_PTR(ramdisk + ramdisk_size));

      return false;
   }

   *file_paddr = NULL;
   len = fat_read_whole_file(hdr, e, (void *)free_space, e->DIR_FileSize);

   if (len != e->DIR_FileSize) {
      printk("ERROR: Couldn't read the whole file\n");
      return false;
   }

   *file_paddr = (void *)free_space;
   return true;
}

void bootloader_main(void)
{
   multiboot_info_t *mbi;
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

   write_bootloader_hello_msg();

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
                    &initrd_paddr,
                    &initrd_size,
                    true);       /* alloc_extra_page */

   /* Set bootpart's lowest paddr, leaving some safe margin */
   bp_min_paddr = initrd_paddr + initrd_size + 2 * PAGE_SIZE;

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
   initrd_size = do_ramdisk_compact_clusters((void *)initrd_paddr, initrd_size);

   /*
    * Increase initrd_size by 1 page in order to allow Tilck's kernel to
    * align the first data sector, if necessary.
    */
   initrd_size += 4 * KB;

   success = common_bootloader_logic();

   if (!success)
      panic("Boot aborted");

   entry = load_kernel_image();
   mbi = setup_multiboot_info(initrd_paddr, initrd_size);

   /* Jump to the kernel */
   asmVolatile("jmp *%%ecx"
               : /* no output */
               : "a" (MULTIBOOT_BOOTLOADER_MAGIC),
                 "b" (mbi),
                 "c" (entry)
               : /* no clobber */);
}
