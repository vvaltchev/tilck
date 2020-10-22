/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>
#include <tilck/boot/common.h>

#undef ASSERT
#include "defs.h"
#include "utils.h"

static void
efi_boot_write_char(char c)
{
   Print(L"%c", c);
}

static int
efi_boot_read_key(void)
{
   EFI_INPUT_KEY k = WaitForKeyPress();
   return k.UnicodeChar & 0xff;
}

static bool
efi_boot_get_mode_info(void *ctx,
                       video_mode_t m,
                       void *opaque_info,
                       struct generic_video_mode_info *gi)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **mi_ref = opaque_info;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt = ctx;
   UINTN sizeof_info = 0;
   EFI_STATUS status;

   *mi_ref = NULL;
   status = gProt->QueryMode(gProt, m, &sizeof_info, mi_ref);

   if (EFI_ERROR(status))
      return false;

   mi = *mi_ref;

   gi->xres = mi->HorizontalResolution;
   gi->yres = mi->VerticalResolution;
   gi->bpp = 0;

   if (mi->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ||
       mi->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
   {
      gi->bpp = 32;
   }

   return true;
}

static bool
efi_boot_is_mode_usable(void *ctx, void *opaque_info)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **mi_ref = opaque_info;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = *mi_ref;
   return IsSupported(mi);
}

const struct bootloader_intf efi_boot_intf = {
   .get_mode_info = &efi_boot_get_mode_info,
   .is_mode_usable = &efi_boot_is_mode_usable,
   .read_key = &efi_boot_read_key,
   .write_char = &efi_boot_write_char,
};
