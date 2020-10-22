/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/boot/gfx.h>

#include "realmode_call.h"
#include "vbe.h"
#include "common.h"

static void debug_show_detailed_mode_info(struct ModeInfoBlock *mi)
{
   printk("Detailed mode info:\n");
   printk("fb_paddr: %p\n", TO_PTR(fb_paddr));
   printk("fb_width: %u\n", fb_width);
   printk("fb_height: %u\n", fb_height);
   printk("fb_pitch: %u\n", fb_pitch);
   printk("fb_bpp: %u\n", fb_bpp);
   printk("LinBytesPerScanLine: %u\n", mi->LinBytesPerScanLine);
   printk("MemoryModel: 0x%x\n", mi->MemoryModel);

   printk("[ red ] mask size: %u, pos: %u\n",
          mi->RedMaskSize, mi->RedFieldPosition);
   printk("[green] mask size: %u, pos: %u\n",
          mi->GreenMaskSize, mi->GreenFieldPosition);
   printk("[blue ] mask size: %u, pos: %u\n",
          mi->BlueMaskSize, mi->BlueFieldPosition);

   printk("Press ANY key to boot\n");
   bios_read_char();
}

static int
legacy_boot_count_modes(video_mode_t *modes)
{
   int cnt = 0;

   while (modes[cnt] != INVALID_VIDEO_MODE)
      cnt++;

   return cnt;
}

void ask_user_video_mode(struct mem_info *minfo)
{
   static video_mode_t ok_modes[16];

   ulong free_mem;
   struct VbeInfoBlock *vb;
   struct ModeInfoBlock *mi;
   video_mode_t *all_modes;
   int all_modes_cnt;

   struct ok_modes_info okm = {
      .ok_modes = ok_modes,
      .ok_modes_array_size = ARRAY_SIZE(ok_modes),
      .ok_modes_cnt = 0,
      .defmode = INVALID_VIDEO_MODE,
   };

   free_mem = get_usable_mem(minfo, 0x1000, 4 * KB);

   if (!free_mem) {
      printk("Unable to allocate memory for VbeInfoBlock\n");
      return;
   }

   vb = (void *)free_mem;
   free_mem = get_usable_mem(minfo, free_mem + 4 * KB, 4 * KB);

   if (!free_mem) {
      printk("Unable to allocate memory for struct ModeInfoBlock\n");
      return;
   }

   mi = (void *)free_mem;

   if (!vbe_get_info_block(vb)) {

      if (BOOT_INTERACTIVE) {
         printk("VBE get info failed. Only the text mode is available.\n");
         printk("Press ANY key to boot in text mode\n");
         bios_read_char();
      }

      return;
   }

   if (vb->VbeVersion < 0x200) {

      if (BOOT_INTERACTIVE) {
         printk("VBE older than 2.0 is not supported.\n");
         printk("Press ANY key to boot in text mode\n");
         bios_read_char();
      }

      return;
   }

   all_modes = get_flat_ptr(vb->VideoModePtr);
   all_modes_cnt = legacy_boot_count_modes(all_modes);

   ok_modes[0] = VGA_COLOR_TEXT_MODE_80x25;

   if (BOOT_INTERACTIVE)
      printk("Mode [0]: text mode 80 x 25\n");

   filter_video_modes(all_modes,all_modes_cnt,mi,BOOT_INTERACTIVE,32,1,&okm);

   if (okm.ok_modes_cnt == 1) {

      /*
       * Extremely unfortunate case: no modes with bpp = 32 are available.
       * Therefore, allow modes with bpp = 24 and iterate again all of over
       * the available modes.
       */

      filter_video_modes(all_modes,all_modes_cnt,mi,BOOT_INTERACTIVE,24,1,&okm);
   }

   selected_mode = BOOT_INTERACTIVE
      ? get_user_video_mode_choice(&okm)
      : okm.defmode;

   if (selected_mode == INVALID_VIDEO_MODE)
      panic("Unable to determine a default video mode");

   if (selected_mode == VGA_COLOR_TEXT_MODE_80x25) {
      graphics_mode = false;
      return;
   }

   if (!vbe_get_mode_info(selected_mode, mi))
      panic("vbe_get_mode_info(0x%x) failed", selected_mode);

   graphics_mode = true;
   fb_paddr = mi->PhysBasePtr;
   fb_width = mi->XResolution;
   fb_height = mi->YResolution;
   fb_pitch = mi->BytesPerScanLine;
   fb_bpp = mi->BitsPerPixel;

   fb_red_pos = mi->RedFieldPosition;
   fb_red_mask_size = mi->RedMaskSize;
   fb_green_pos = mi->GreenFieldPosition;
   fb_green_mask_size = mi->GreenMaskSize;
   fb_blue_pos = mi->BlueFieldPosition;
   fb_blue_mask_size = mi->BlueMaskSize;

   if (vb->VbeVersion >= 0x300)
      fb_pitch = mi->LinBytesPerScanLine;
}
