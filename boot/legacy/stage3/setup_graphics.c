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

static video_mode_t
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

static bool
legacy_boot_get_mode_info(void *ctx,
                          video_mode_t m,
                          void *opaque_info,
                          struct generic_video_mode_info *gi)
{
   if (vbe_get_mode_info(m, opaque_info)) {

      if (gi) {

         struct ModeInfoBlock *mi = opaque_info;

         gi->xres = mi->XResolution;
         gi->yres = mi->YResolution;
         gi->bpp = mi->BitsPerPixel;
      }

      return true;
   }

   return false;
}

static bool
legacy_boot_is_mode_usable(void *ctx, void *opaque_info)
{
   struct ModeInfoBlock *mi = opaque_info;

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

static void
legacy_boot_show_mode(void *ctx, int num, void *opaque_info, bool is_default)
{
   struct ModeInfoBlock *mi = opaque_info;

   printk("Mode [%d]: %d x %d x %d%s\n",
          num, mi->XResolution, mi->YResolution,
          mi->BitsPerPixel, is_default ? " [DEFAULT]" : "");
}

static const struct bootloader_intf legacy_boot_intf = {
   .get_mode_info = &legacy_boot_get_mode_info,
   .is_mode_usable = &legacy_boot_is_mode_usable,
   .show_mode = &legacy_boot_show_mode,
};

static int
legacy_boot_count_modes(video_mode_t *modes)
{
   int cnt = 0;

   while (modes[cnt] != INVALID_VIDEO_MODE)
      cnt++;

   return cnt;
}

static void
filter_modes(video_mode_t *all_modes,
             int all_modes_cnt,
             void *opaque_mi,
             bool show_modes,
             int bpp,
             int ok_modes_start,
             struct ok_modes_info *okm)
{
   filter_video_modes(&legacy_boot_intf,
                      all_modes,
                      all_modes_cnt,
                      opaque_mi,
                      show_modes,
                      bpp,
                      ok_modes_start,
                      okm,
                      NULL);
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

   filter_modes(all_modes, all_modes_cnt, mi, BOOT_INTERACTIVE, 32, 1, &okm);

   if (okm.ok_modes_cnt == 1) {

      /*
       * Extremely unfortunate case: no modes with bpp = 32 are available.
       * Therefore, allow modes with bpp = 24 and iterate again all of over
       * the available modes.
       */

      filter_modes(all_modes, all_modes_cnt, mi, BOOT_INTERACTIVE, 24, 1, &okm);
   }

   selected_mode = BOOT_INTERACTIVE
      ? do_get_user_video_mode_choice(&okm)
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
