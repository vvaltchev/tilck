/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/string_util.h>
#include <tilck/boot/common.h>

#include "basic_term.h"
#include "vbe.h"

static int
legacy_boot_read_key(void)
{
   return bios_read_char();
}

static bool
legacy_boot_get_mode_info(video_mode_t m,
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
legacy_boot_is_mode_usable(void *opaque_info)
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
legacy_boot_clear_screen(void)
{
   init_bt();
}

const struct bootloader_intf legacy_boot_intf = {
   .get_mode_info = &legacy_boot_get_mode_info,
   .is_mode_usable = &legacy_boot_is_mode_usable,
   .read_key = &legacy_boot_read_key,
   .write_char = &bt_write_char,
   .clear_screen = &legacy_boot_clear_screen,
};
