/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>
#include <tilck/boot/common.h>

#undef ASSERT
#include "defs.h"
#include "utils.h"

static int
ReadAsciiLine(char *buf, int buf_sz)
{
   EFI_INPUT_KEY k;
   int len = 0;
   CHAR16 uc;

   while (true) {

      k = WaitForKeyPress();
      uc = k.UnicodeChar;

      if (uc == '\n' || uc == '\r') {
         Print(L"\r\n");
         break;
      }

      if (!isprint(uc)) {

         if (uc == '\b' && len > 0) {
            Print(L"\b \b");
            len--;
         }

         continue;
      }

      if (len < buf_sz - 1) {
         Print(L"%c", uc);
         buf[len++] = (char)uc;
      }
   }

   buf[len] = 0;
   return len;
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

static void
efi_boot_show_mode(void *ctx, int num, void *opaque_info, bool is_default)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **mi_ref = opaque_info;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = *mi_ref;

   Print(L"Mode [%d]: %u x %u x 32%s\n",
         num,
         mi->HorizontalResolution,
         mi->VerticalResolution,
         is_default ? L" [DEFAULT]" : L"");
}

const struct bootloader_intf efi_boot_intf = {
   .get_mode_info = &efi_boot_get_mode_info,
   .is_mode_usable = &efi_boot_is_mode_usable,
   .show_mode = &efi_boot_show_mode,
   .read_line = &ReadAsciiLine,
};
