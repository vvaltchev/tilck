/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/string_util.h>
#include <tilck/boot/common.h>
#include "vbe.h"

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

const struct bootloader_intf legacy_boot_intf = {
   .get_mode_info = &legacy_boot_get_mode_info,
   .is_mode_usable = &legacy_boot_is_mode_usable,
   .read_line = &bios_read_line,
};
