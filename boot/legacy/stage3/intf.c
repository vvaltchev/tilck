/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/string_util.h>
#include <tilck/boot/common.h>

#include "basic_term.h"
#include "vbe.h"
#include "common.h"

struct ModeInfoBlock *usable_vbe_mode_info_block;
struct VbeInfoBlock *vbe_info_block;

static int
legacy_boot_count_modes(video_mode_t *modes)
{
   int cnt = 0;

   while (modes[cnt] != INVALID_VIDEO_MODE)
      cnt++;

   return cnt;
}

static void
alloc_once_vbe_info_blocks(void)
{
   ulong free_mem;

   if (!vbe_info_block) {

      free_mem = get_usable_mem(&g_meminfo, 0x1000, 4 * KB);

      if (!free_mem)
         panic("Unable to allocate memory for VbeInfoBlock\n");

      vbe_info_block = (void *)free_mem;

      free_mem = get_usable_mem(&g_meminfo, free_mem + 4 * KB, 4 * KB);

      if (!free_mem)
         panic("Unable to allocate memory for struct ModeInfoBlock\n");

      usable_vbe_mode_info_block = (void *)free_mem;
   }
}

static bool
is_mode_usable(struct ModeInfoBlock *mi)
{
   if (!(mi->ModeAttributes & VBE_MODE_ATTRS_GFX_MODE))
      return false;

   if (!(mi->ModeAttributes & VBE_MODE_ATTRS_LINEAR_FB))
      return false;

   if (!(mi->ModeAttributes & VBE_MODE_ATTRS_SUPPORTED))
      return false;

   if (mi->MemoryModel != VB_MEM_MODEL_DIRECT_COLOR)
      return false;

   return true;
}

static int
legacy_boot_read_key(void)
{
   return bios_read_char();
}

static bool
legacy_boot_get_mode_info(video_mode_t m, struct generic_video_mode_info *gi)
{
   struct ModeInfoBlock *mi = usable_vbe_mode_info_block;
   bool success = false;

   if (m == VGA_COLOR_TEXT_MODE_80x25) {

      gi->xres = 80;
      gi->yres = 25;
      gi->bpp = 4;
      gi->is_text_mode = true;
      gi->is_usable = true;
      success = true;

   } else if (vbe_get_mode_info(m, mi)) {

      gi->xres = mi->XResolution;
      gi->yres = mi->YResolution;
      gi->bpp = mi->BitsPerPixel;
      gi->is_text_mode = false;
      gi->is_usable = is_mode_usable(mi);
      success = true;
   }

   return success;
}

static void
legacy_boot_clear_screen(void)
{
   init_bt();
}

static video_mode_t
legacy_boot_get_curr_video_mode(void)
{
   return selected_mode;
}

static bool
legacy_boot_set_curr_video_mode(video_mode_t wanted_mode)
{
   if (!vbe_set_video_mode(wanted_mode))
      return false;

   selected_mode = wanted_mode;
   return true;
}

static void
legacy_boot_get_all_video_modes(video_mode_t **modes, int *count)
{
   video_mode_t *all_modes = NULL;
   struct VbeInfoBlock *vb;
   int all_modes_cnt = 0;

   alloc_once_vbe_info_blocks();
   vb = vbe_info_block;

   if (!vbe_get_info_block(vb)) {

      if (BOOT_INTERACTIVE)
         printk("VBE get info failed. Only the text mode is available.\n");

      vb = NULL;
   }

   if (vb->VbeVersion < 0x200) {

      if (BOOT_INTERACTIVE)
         printk("VBE older than 2.0 is not supported.\n");

      vb = NULL;
   }

   if (vb) {
      all_modes = get_flat_ptr(vb->VideoModePtr);
      all_modes_cnt = legacy_boot_count_modes(all_modes);
   }

   *modes = all_modes;
   *count = all_modes_cnt;
}

static bool
legacy_boot_load_kernel_file(const char *path, void **paddr)
{
   if (!load_kernel_file(path))
      return false;

   *paddr = TO_PTR(kernel_file_pa);
   return true;
}

static bool
legacy_boot_load_initrd(void)
{
   bool success;

   if (!kernel_file_pa)
      panic("No loaded kernel file");

   success =
      load_fat_ramdisk(LOADING_INITRD_STR,
                       INITRD_SECTOR,
                       KERNEL_PADDR + get_loaded_kernel_mem_sz(),
                       &initrd_paddr,
                       &initrd_size,
                       true);       /* alloc_extra_page */

   if (!success)
      return false;

   /* Compact initrd's clusters, if necessary */
   initrd_size = rd_compact_clusters((void *)initrd_paddr, initrd_size);

   /*
    * Increase initrd_size by 1 page in order to allow Tilck's kernel to
    * align the first data sector, if necessary.
    */
   initrd_size += 4 * KB;
   return true;
}

const struct bootloader_intf legacy_boot_intf = {

   /* Methods */
   .read_key = &legacy_boot_read_key,
   .write_char = &bt_write_char,
   .clear_screen = &legacy_boot_clear_screen,
   .set_color = &bt_setcolor,
   .get_all_video_modes = &legacy_boot_get_all_video_modes,
   .get_mode_info = &legacy_boot_get_mode_info,
   .get_curr_video_mode = &legacy_boot_get_curr_video_mode,
   .set_curr_video_mode = &legacy_boot_set_curr_video_mode,
   .load_kernel_file = &legacy_boot_load_kernel_file,
   .load_initrd = &legacy_boot_load_initrd,
   .get_cmdline_buf = &legacy_boot_get_cmdline_buf,

   /* Configuration values */
   .text_mode = VGA_COLOR_TEXT_MODE_80x25,
   .efi = false,
};
