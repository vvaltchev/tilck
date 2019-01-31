/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/gfx.h>

#include "realmode_call.h"
#include "vbe.h"

extern u32 fb_paddr;
extern u32 fb_pitch;
extern u32 fb_width;
extern u32 fb_height;
extern u32 fb_bpp;

extern u8 fb_red_pos;
extern u8 fb_red_mask_size;
extern u8 fb_green_pos;
extern u8 fb_green_mask_size;
extern u8 fb_blue_pos;
extern u8 fb_blue_mask_size;

extern bool graphics_mode;
extern u16 selected_mode;

void debug_show_detailed_mode_info(ModeInfoBlock *mi)
{
   printk("Detailed mode info:\n");
   printk("fb_paddr: %p\n", fb_paddr);
   printk("fb_width: %u\n", fb_width);
   printk("fb_height: %u\n", fb_height);
   printk("fb_pitch: %u\n", fb_pitch);
   printk("fb_bpp: %u\n", fb_bpp);
   printk("LinBytesPerScanLine: %u\n", mi->LinBytesPerScanLine);
   printk("MemoryModel: 0x%x\n", mi->MemoryModel);

   printk("[ red ] mask size: %u, pos: %u\n", mi->RedMaskSize, mi->RedFieldPosition);
   printk("[green] mask size: %u, pos: %u\n", mi->GreenMaskSize, mi->GreenFieldPosition);
   printk("[blue ] mask size: %u, pos: %u\n", mi->BlueMaskSize, mi->BlueFieldPosition);

   printk("Press ANY key to boot\n");
   bios_read_char();
}

static void show_single_mode(int num, ModeInfoBlock *mi)
{
   printk("Mode [%d]: %d x %d x %d\n",
          num, mi->XResolution, mi->YResolution, mi->BitsPerPixel);
}

static void show_modes_aux(u16 *modes,
                           ModeInfoBlock *mi,
                           u16 *known_modes,
                           int *known_modes_count,
                           int min_bpp)
{
   u32 max_width = 0;
   u16 max_width_mode;

   for (u32 i = 0; modes[i] != 0xffff; i++) {

      if (!vbe_get_mode_info(modes[i], mi))
         continue;

      /* skip text modes */
      if (!(mi->ModeAttributes & VBE_MODE_ATTRS_GFX_MODE))
         continue;

      /* skip graphics mode not supporting a linear framebuffer */
      if (!(mi->ModeAttributes & VBE_MODE_ATTRS_LINEAR_FB))
         continue;

      if (!(mi->ModeAttributes & VBE_MODE_ATTRS_SUPPORTED))
         continue;

      if (mi->MemoryModel != VB_MEM_MODEL_DIRECT_COLOR)
         continue;

      if (mi->BitsPerPixel < min_bpp)
         continue;

      if (!is_tilck_known_resolution(mi->XResolution, mi->YResolution)) {

         if (mi->XResolution > max_width) {
            max_width = mi->XResolution;
            max_width_mode = modes[i];
         }

         continue;
      }

      show_single_mode(*known_modes_count, mi);
      known_modes[(*known_modes_count)++] = modes[i];
   }

   if (max_width) {

      if (!vbe_get_mode_info(max_width_mode, mi))
         panic("vbe_get_mode_info(0x%x) failed", max_width_mode);

      show_single_mode(*known_modes_count, mi);
      known_modes[(*known_modes_count)++] = max_width_mode;
   }
}

void ask_user_video_mode(void)
{
   VbeInfoBlock *vb = (void *)0x2000;
   ModeInfoBlock *mi = (void *)0x3000;

   u16 known_modes[10];
   int known_modes_count = 0;

   if (!vbe_get_info_block(vb)) {
      printk("VBE get info failed. Only the text mode is available.\n");
      printk("Press ANY key to boot in text mode\n");
      bios_read_char();
      return;
   }

   if (vb->VbeVersion < 0x200) {
      printk("VBE older than 2.0 is not supported.\n");
      printk("Press ANY key to boot in text mode\n");
      bios_read_char();
      return;
   }

   known_modes[known_modes_count++] = VGA_COLOR_TEXT_MODE_80x25;
   printk("Mode [0]: text mode 80 x 25 [DEFAULT]\n");


   u16 *modes = get_flat_ptr(vb->VideoModePtr);
   show_modes_aux(modes, mi, known_modes, &known_modes_count, 32);

   if (known_modes_count == 1) {

      /*
       * Extremely unfortunate case: no modes with bpp = 32 are available.
       * Therefore, allow modes with bpp = 24 and iterate again all of over
       * the available modes.
       */

      show_modes_aux(modes, mi, known_modes, &known_modes_count, 24);
   }

   printk("\n");

   while (true) {

      printk("Select a video mode [%d - %d]: ", 0, known_modes_count - 1);

      char sel = bios_read_char();
      int s = sel - '0';

      if (sel == '\r') {
         selected_mode = VGA_COLOR_TEXT_MODE_80x25;
         printk("DEFAULT\n");
         break;
      }

      if (s < 0 || s > known_modes_count - 1) {
         printk("Invalid selection.\n");
         continue;
      }

      printk("%d\n\n", s);
      selected_mode = known_modes[s];
      break;
   }

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

   //debug_show_detailed_mode_info(mi);
}
