/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>

#include "defs.h"
#include "utils.h"

#include <multiboot.h>

static void PrintModeInfo(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi)
{
   Print(L"Resolution: %u x %u\n",
         mi->HorizontalResolution,
         mi->VerticalResolution);

   if (mi->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
      Print(L"PixelFormat: RGB + reserved\n");
   else if (mi->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
      Print(L"PixelFormat: BGR + reserved\n");
   else
      Print(L"PixelFormat: other\n");

   Print(L"PixelsPerScanLine: %u\n", mi->PixelsPerScanLine);
}

static void PrintModeFullInfo(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode)
{
   Print(L"Framebuffer addr: 0x%x\n", mode->FrameBufferBase);
   Print(L"Framebuffer size: %u\n", mode->FrameBufferSize);
   PrintModeInfo(mode->Info);
}

static bool IsSupported(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi)
{
   if (sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) != 4)
      return false;

   if (!is_tilck_usable_resolution(mi->HorizontalResolution,
                                   mi->VerticalResolution))
   {
      return false;
   }

   return mi->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ||
          mi->PixelFormat == PixelRedGreenBlueReserved8BitPerColor;
}

static bool IsKnownAndSupported(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi)
{
   if (is_tilck_known_resolution(mi->HorizontalResolution,
                                 mi->VerticalResolution))
   {
      return IsSupported(mi);
   }

   return false;
}

static bool IsTilckDefaultMode(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi)
{
   return IsSupported(mi) &&
            is_tilck_default_resolution(mi->HorizontalResolution,
                                        mi->VerticalResolution);
}

/*
 * Find a good video mode for the bootloader itself.
 *
 * This function is called in EarlySetDefaultResolution(), before displaying
 * anything on the screen. It solves the problem with modern machines with
 * "retina" displays, where just using the native resolution with the default
 * EFI font results in extremely tiny text, a pretty bad user experience.
 */
static EFI_STATUS
FindGoodVideoMode(EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt,
                  bool supported,
                  INTN *choice)
{
   INTN chosenMode = -1, minResPixels = 0, minResModeN = -1;
   EFI_STATUS status = EFI_SUCCESS;

   for (UINTN i = 0; i < gProt->Mode->MaxMode; i++) {

      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
      UINTN sizeof_info = 0;

      status = gProt->QueryMode(gProt, i, &sizeof_info, &mi);
      HANDLE_EFI_ERROR("QueryMode() failed");

      if (supported && !IsSupported(mi))
         continue;

      /*
       * NOTE: it's fine to use a resolution not supported by Tilck here.
       * We just need any good-enough and low resolution for the displaying
       * stuff on the screen.
       */

      if (mi->HorizontalResolution == PREFERRED_GFX_MODE_W &&
          mi->VerticalResolution == PREFERRED_GFX_MODE_H)
      {
         chosenMode = (INTN) i;
         break; /* Our preferred resolution */
      }

      const INTN p = (INTN)(mi->HorizontalResolution * mi->VerticalResolution);

      if (p < minResPixels) {
         minResPixels = p;
         minResModeN = (INTN) i;
      }
   }

   if (chosenMode >= 0)
      *choice = chosenMode;
   else if (minResModeN >= 0)
      *choice = minResModeN;
   else
      *choice = -1;

end:
   return status;
}

EFI_STATUS
EarlySetDefaultResolution(void)
{
   EFI_STATUS status;
   EFI_HANDLE handles[32];
   UINTN handles_buf_size;
   UINTN handles_count;
   INTN origMode, chosenMode = -1;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt;

   ST->ConOut->ClearScreen(ST->ConOut);
   handles_buf_size = sizeof(handles);

   status = BS->LocateHandle(ByProtocol,
                             &GraphicsOutputProtocol,
                             NULL,
                             &handles_buf_size,
                             handles);

   HANDLE_EFI_ERROR("LocateHandle() failed");

   handles_count = handles_buf_size / sizeof(EFI_HANDLE);
   CHECK(handles_count > 0);

   status = BS->HandleProtocol(handles[0],
                               &GraphicsOutputProtocol,
                               (void **)&gProt);
   HANDLE_EFI_ERROR("HandleProtocol() failed");

   FindGoodVideoMode(gProt, true, &chosenMode);
   HANDLE_EFI_ERROR("FindGoodVideoMode() failed");

   if (chosenMode < 0) {

      /*
       * We were unable to find a good and supported (= 32 bps) video mode.
       * That's bad, but not fatal: just re-run FindGoodVideoMode() including
       * also non-32bps video modes. They are still perfectly fine for the
       * bootloader. The resolution used by Tilck instead, will be chosen later
       * directly by the user, among the available ones.
       */

      FindGoodVideoMode(gProt, false, &chosenMode);
      HANDLE_EFI_ERROR("FindGoodVideoMode() failed");

      if (chosenMode < 0) {
         /* Do nothing: just keep the current video mode */
         return status;
      }
   }

   origMode = (INTN) gProt->Mode->Mode;

   if (chosenMode == origMode)
      return status; /* We're already using a "good" video mode */

   status = gProt->SetMode(gProt, (UINTN)chosenMode);

   if (EFI_ERROR(status)) {
      /* Something went wrong: just restore the previous video mode */
      status = gProt->SetMode(gProt, (UINTN)origMode);
      HANDLE_EFI_ERROR("SetMode() failed");
   }

end:
   return status;
}

static void
PrintFailedModeInfo(EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt, UINTN failed_mode)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
   UINTN sizeof_info = 0;
   EFI_STATUS status;

   status = gProt->QueryMode(gProt, failed_mode, &sizeof_info, &mi);

   if (!EFI_ERROR(status)) {
      Print(L"Failed mode info:\r\n");
      PrintModeInfo(mi);
   } else {
      Print(L"ERROR: Unable to print failed mode info: %r\r\n", status);
   }
}

static bool
SwitchToUserSelectedMode(EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt,
                         UINTN wanted_mode,
                         UINTN orig_mode)
{
   EFI_STATUS status;

   ST->ConOut->ClearScreen(ST->ConOut);    /* NOTE: do not handle failures */
   status = gProt->SetMode(gProt, wanted_mode);

   if (EFI_ERROR(status)) {

      gProt->SetMode(gProt, orig_mode);    /* NOTE: do not handle failures */
      ST->ConOut->ClearScreen(ST->ConOut); /* NOTE: do not handle failures */

      Print(L"ERROR: Unable to set desired mode: %r\r\n", status);
      PrintFailedModeInfo(gProt, wanted_mode);
      return false;
   }

   return true;
}

static bool
ExistsModeInArray(u32 mode, u32 *arr, u32 arr_size)
{
   for (u32 i = 0; i < arr_size; i++)
      if (arr[i] == mode)
         return true;

   return false;
}

static UINTN
GetUserModeChoice(u32 *my_modes, u32 my_modes_count, u32 default_mode)
{
   char buf[16];
   UINTN len;
   long sel;
   int err;

   while (true) {

      Print(L"Select mode [0 - %d]: ", my_modes_count - 1);
      len = ReadAsciiLine(buf, sizeof(buf));

      if (!len) {
         Print(L"<default>\r\n\r\n");
         return default_mode;
      }

      sel = tilck_strtol(buf, NULL, 10, &err);

      if (err || sel < 0 || sel >= (long)my_modes_count) {
         Print(L"Invalid selection\n");
         continue;
      }

      break;
   }

   return my_modes[sel];
}

EFI_STATUS
SetupGraphicMode(UINTN *fb_addr,
                 EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info)
{
   EFI_STATUS status;
   EFI_HANDLE handles[32];
   UINTN handles_buf_size;
   UINTN handles_count;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt;
   EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;

   UINTN wanted_mode;
   UINTN orig_mode;
   UINTN default_mode = (UINTN)-1;

   u32 my_modes[16];
   u32 my_modes_count = 0;
   u32 max_mode_pixels = 0;
   u32 max_mode_num = 0;
   u32 max_mode_xres = 0;
   u32 max_mode_yres = 0;


   handles_buf_size = sizeof(handles);

   status = BS->LocateHandle(ByProtocol,
                             &GraphicsOutputProtocol,
                             NULL,
                             &handles_buf_size,
                             handles);

   HANDLE_EFI_ERROR("LocateHandle() failed");

   handles_count = handles_buf_size / sizeof(EFI_HANDLE);
   CHECK(handles_count > 0);

   status = BS->HandleProtocol(handles[0],
                               &GraphicsOutputProtocol,
                               (void **)&gProt);
   HANDLE_EFI_ERROR("HandleProtocol() failed");

   mode = gProt->Mode;
   orig_mode = mode->Mode;

   if (!BOOT_ASK_VIDEO_MODE)
      goto after_user_choice;

retry:

   for (UINTN i = 0; i < mode->MaxMode; i++) {

      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
      UINTN sizeof_info = 0;
      u32 pixels;

      status = gProt->QueryMode(gProt, i, &sizeof_info, &mi);
      HANDLE_EFI_ERROR("QueryMode() failed");

      if (!IsSupported(mi))
         continue;

      pixels = mi->HorizontalResolution * mi->VerticalResolution;

      if (pixels > max_mode_pixels) {
         max_mode_pixels = pixels;
         max_mode_num = i;
         max_mode_xres = mi->HorizontalResolution;
         max_mode_yres = mi->VerticalResolution;
      }

      if (!IsKnownAndSupported(mi))
         continue;

      if (IsTilckDefaultMode(mi))
         default_mode = i;

      if (my_modes_count < ARRAY_SIZE(my_modes) - 1) {
         Print(L"Mode [%u]: %u x %u%s\n",
               my_modes_count,
               mi->HorizontalResolution,
               mi->VerticalResolution,
               i == default_mode ? L" [DEFAULT]" : L"");

         my_modes[my_modes_count++] = i;
      }
   }

   if (!ExistsModeInArray(max_mode_num, my_modes, my_modes_count)) {

      Print(L"Mode [%u]: %u x %u%s\n",
            my_modes_count, max_mode_xres,
            max_mode_yres, max_mode_num == default_mode ? L" [DEFAULT]" : L"");

      my_modes[my_modes_count++] = max_mode_num;
   }

   if (!my_modes_count) {
      Print(L"No supported modes available\n");
      status = EFI_LOAD_ERROR;
      goto end;
   }

   wanted_mode = GetUserModeChoice(my_modes, my_modes_count, default_mode);

   if (wanted_mode != orig_mode) {
      if (!SwitchToUserSelectedMode(gProt, wanted_mode, orig_mode))
         goto retry;
   }

after_user_choice:

   mode = gProt->Mode;
   *fb_addr = mode->FrameBufferBase;
   *mode_info = *mode->Info;

end:
   return status;
}
