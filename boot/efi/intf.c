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
efi_boot_get_mode_info(video_mode_t m, struct generic_video_mode_info *gi)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
   UINTN sizeof_info = 0;
   EFI_STATUS status;

   status = gProt->QueryMode(gProt, m, &sizeof_info, &mi);

   if (EFI_ERROR(status))
      return false;

   gi->xres = mi->HorizontalResolution;
   gi->yres = mi->VerticalResolution;
   gi->bpp = 0;
   gi->is_text_mode = false;
   gi->is_usable = false;

   if (mi->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ||
       mi->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
   {
      gi->bpp = 32;
      gi->is_usable = true;
   }

   return true;
}

static void
efi_boot_clear_screen(void)
{
   ST->ConOut->ClearScreen(ST->ConOut);
}

static video_mode_t
efi_boot_get_curr_video_mode(void)
{
   return gProt->Mode->Mode;
}

bool
efi_boot_set_curr_video_mode(video_mode_t wanted_mode)
{
   video_mode_t orig_mode = gProt->Mode->Mode;
   EFI_STATUS status = EFI_SUCCESS;

   if (wanted_mode != orig_mode) {

      ST->ConOut->ClearScreen(ST->ConOut);    /* NOTE: do not handle failures */
      status = gProt->SetMode(gProt, wanted_mode);

      if (EFI_ERROR(status)) {

         gProt->SetMode(gProt, orig_mode);    /* NOTE: do not handle failures */
         ST->ConOut->ClearScreen(ST->ConOut); /* NOTE: do not handle failures */
      }
   }

   return status == EFI_SUCCESS;
}

static void
efi_boot_get_all_video_modes(video_mode_t **modes, int *count)
{
   *modes = NULL;
   *count = (int)gProt->Mode->MaxMode;
}

static bool
efi_boot_load_kernel_file(const char *path, void **paddr)
{
   static CHAR16 tmp_path[128];

   EFI_PHYSICAL_ADDRESS kernel_file_paddr;
   const char *p = path;
   int i;

   for (i = 0; *p && i < ARRAY_SIZE(tmp_path); i++, p++) {
      tmp_path[i] = *p != '/' ? *p : '\\';
   }

   if (i == ARRAY_SIZE(tmp_path)) {
      Print(L"File path too long\n");
      return false;
   }

   tmp_path[i] = 0;

   if (EFI_ERROR(LoadKernelFile(tmp_path, &kernel_file_paddr)))
      return false;

   *paddr = TO_PTR(kernel_file_paddr);
   return true;
}

static void
efi_boot_set_color(u8 color)
{
   /* do nothing */
}

static bool
efi_boot_load_initrd(void)
{
   EFI_STATUS status;

   status = LoadRamdisk(gImageHandle,
                        gLoadedImage,
                        &gRamdiskPaddr,
                        &gRamdiskSize,
                        2); /* CurrConsoleRow (HACK). See ShowProgress() */

   HANDLE_EFI_ERROR("LoadRamdisk failed");

end:
   return status == EFI_SUCCESS;
}

const struct bootloader_intf efi_boot_intf = {

   /* Methods */
   .read_key = &efi_boot_read_key,
   .write_char = &efi_boot_write_char,
   .clear_screen = &efi_boot_clear_screen,
   .set_color = &efi_boot_set_color,
   .get_all_video_modes = &efi_boot_get_all_video_modes,
   .get_mode_info = &efi_boot_get_mode_info,
   .get_curr_video_mode = &efi_boot_get_curr_video_mode,
   .set_curr_video_mode = &efi_boot_set_curr_video_mode,
   .load_kernel_file = &efi_boot_load_kernel_file,
   .load_initrd = &efi_boot_load_initrd,

   /* Configuration values */
   .text_mode = INVALID_VIDEO_MODE,
   .efi = true,
};
