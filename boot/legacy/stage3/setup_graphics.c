/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/gfx.h>

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

static void
show_single_mode(int num, struct ModeInfoBlock *mi, bool default_mode)
{
   printk("Mode [%d]: %d x %d x %d%s\n",
          num, mi->XResolution,
          mi->YResolution, mi->BitsPerPixel, default_mode ? " [DEFAULT]" : "");
}

static bool
exists_mode_in_array(u16 mode, u16 *arr, int array_sz)
{
   for (int i = 0; i < array_sz; i++)
      if (arr[i] == mode)
         return true;

   return false;
}

static int
bios_read_line(char *buf, int buf_sz)
{
   int len = 0;
   char c;

   while (true) {

      c = bios_read_char();

      if (c == '\r') {
         printk("\n");
         break;
      }

      if (!isprint(c)) {

         if (c == '\b' && len > 0) {
            printk("\b \b");
            len--;
         }

         continue;
      }

      if (len < buf_sz - 1) {
         printk("%c", c);
         buf[len++] = c;
      }
   }

   buf[len] = 0;
   return len;
}

struct ok_modes_info {

   u16 *ok_modes;
   int ok_modes_array_size;
   int ok_modes_cnt;
   u16 defmode;
};

static u16
do_get_user_video_mode_choice(struct ok_modes_info *okm)
{
   int len, err = 0;
   char buf[16];
   long s;

   printk("\n");

   while (true) {

      printk("Select a video mode [0 - %d]: ", okm->ok_modes_cnt - 1);

      len = bios_read_line(buf, sizeof(buf));

      if (!len) {
         printk("DEFAULT\n");
         return okm->defmode;
      }

      s = tilck_strtol(buf, NULL, 10, &err);

      if (err || s < 0 || s > okm->ok_modes_cnt - 1) {
         printk("Invalid selection.\n");
         continue;
      }

      break;
   }

   return okm->ok_modes[s];
}

static void
filter_modes(u16 *all_modes,              /* IN */
             struct ModeInfoBlock *mi,    /* IN */
             bool show_modes,             /* IN */
             int bpp,                     /* IN */
             int ok_modes_start,          /* IN */
             struct ok_modes_info *okm)   /* IN/OUT */
{
   u32 max_width = 0;
   u16 max_width_mode = 0;
   int cnt = ok_modes_start;

   okm->defmode = 0xffff;

   for (u32 i = 0; all_modes[i] != 0xffff; i++) {

      if (!vbe_get_mode_info(all_modes[i], mi))
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

      if (mi->BitsPerPixel != bpp)
         continue;

      if (!is_tilck_usable_resolution(mi->XResolution, mi->YResolution))
         continue;

      if (mi->XResolution > max_width) {
         max_width = mi->XResolution;
         max_width_mode = all_modes[i];
      }

      if (!is_tilck_known_resolution(mi->XResolution, mi->YResolution))
         continue;

      if (is_tilck_default_resolution(mi->XResolution, mi->YResolution))
         okm->defmode = all_modes[i];

      if (cnt < okm->ok_modes_array_size - 1) {

         if (show_modes)
            show_single_mode(cnt, mi, okm->defmode == all_modes[i]);

         okm->ok_modes[cnt++] = all_modes[i];
      }
   }

   if (max_width) {

      if (!exists_mode_in_array(max_width_mode, okm->ok_modes, cnt)) {

         if (!vbe_get_mode_info(max_width_mode, mi))
            panic("vbe_get_mode_info(0x%x) failed", max_width_mode);

         if (show_modes)
            show_single_mode(cnt, mi, false);

         okm->ok_modes[cnt++] = max_width_mode;

         if (okm->defmode == 0xffff)
            okm->defmode = max_width_mode;
      }
   }

   if (okm->defmode == 0xffff) {
      if (ok_modes_start > 0)
         okm->defmode = all_modes[0];
   }

   okm->ok_modes_cnt = cnt;
}

void ask_user_video_mode(struct mem_info *minfo)
{
   ulong free_mem;
   struct VbeInfoBlock *vb;
   struct ModeInfoBlock *mi;
   u16 ok_modes[16];
   u16 *all_modes;

   struct ok_modes_info okm = {
      .ok_modes = ok_modes,
      .ok_modes_array_size = ARRAY_SIZE(ok_modes),
      .ok_modes_cnt = 0,
      .defmode = 0xffff,
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

   ok_modes[0] = VGA_COLOR_TEXT_MODE_80x25;

   if (BOOT_INTERACTIVE)
      printk("Mode [0]: text mode 80 x 25\n");

   all_modes = get_flat_ptr(vb->VideoModePtr);
   filter_modes(all_modes, mi, BOOT_INTERACTIVE, 32, 1, &okm);

   if (okm.ok_modes_cnt == 1) {

      /*
       * Extremely unfortunate case: no modes with bpp = 32 are available.
       * Therefore, allow modes with bpp = 24 and iterate again all of over
       * the available modes.
       */

      filter_modes(all_modes, mi, BOOT_INTERACTIVE, 24, 1, &okm);
   }

   selected_mode = BOOT_INTERACTIVE
      ? do_get_user_video_mode_choice(&okm)
      : okm.defmode;

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
