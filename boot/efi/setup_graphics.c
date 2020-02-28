/* SPDX-License-Identifier: BSD-2-Clause */

#include "defs.h"
#include "utils.h"
#include <multiboot.h>

static void PrintModeInfo(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode)
{
   Print(L"Framebuffer addr: 0x%x\n", mode->FrameBufferBase);
   Print(L"Framebuffer size: %u\n", mode->FrameBufferSize);
   Print(L"Resolution: %u x %u\n",
         mode->Info->HorizontalResolution,
         mode->Info->VerticalResolution);

   if (mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
      Print(L"PixelFormat: RGB + reserved\n");
   else if (mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
      Print(L"PixelFormat: BGR + reserved\n");
   else
      Print(L"PixelFormat: other\n");

   Print(L"PixelsPerScanLine: %u\n", mode->Info->PixelsPerScanLine);
}

static bool IsSupported(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi)
{
   if (sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) != 4)
      return false;

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
   if (IsSupported(mi))
      if (mi->HorizontalResolution == 800 && mi->VerticalResolution == 600)
         return true;

   return false;
}

static EFI_STATUS
FindGoodVideoMode(EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt,
                  bool supported,
                  INTN *choice)
{
   INTN chosenMode = -1, minResPixels = 0, minResModeN = -1;
   EFI_STATUS status;

   for (UINTN i = 0; i < gProt->Mode->MaxMode; i++) {

      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
      UINTN sizeof_info = 0;

      status = gProt->QueryMode(gProt, i, &sizeof_info, &mi);
      HANDLE_EFI_ERROR("QueryMode() failed");

      if (mi->HorizontalResolution < 640)
         continue; /* too small */

      if (mi->VerticalResolution < 480)
         continue; /* too small */

      if (supported && !IsSupported(mi))
         continue;

      if (mi->HorizontalResolution == 800 && mi->VerticalResolution == 600) {
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
EarlySetLowResolution(EFI_SYSTEM_TABLE *ST, EFI_BOOT_SERVICES *BS)
{
   EFI_STATUS status;
   EFI_HANDLE handles[32];
   UINTN handles_buf_size;
   UINTN handles_count;
   INTN chosenMode, origMode;
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

EFI_STATUS
SetupGraphicMode(EFI_BOOT_SERVICES *BS,
                 UINTN *fb_addr,
                 EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info)
{
   EFI_STATUS status;
   EFI_HANDLE handles[32];
   UINTN handles_buf_size;
   UINTN handles_count;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt;
   EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
   EFI_INPUT_KEY k;

   UINTN wanted_mode;
   UINTN orig_mode;
   UINTN default_mode = (UINTN)-1;

   u32 my_modes[10];
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

   for (UINTN i = 0; i < mode->MaxMode; i++) {

      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
      UINTN sizeof_info = 0;

      status = gProt->QueryMode(gProt, i, &sizeof_info, &mi);
      HANDLE_EFI_ERROR("QueryMode() failed");

      if (IsTilckDefaultMode(mi)) {
         default_mode = i;
      }

      if (IsKnownAndSupported(mi) || (IsSupported(mi) && i == mode->MaxMode-1))
      {
         Print(L"Mode [%u]: %u x %u%s\n",
               my_modes_count,
               mi->HorizontalResolution,
               mi->VerticalResolution,
               i == default_mode ? L" [DEFAULT]" : L"");

         my_modes[my_modes_count++] = i;
      }

      u32 pixels = mi->HorizontalResolution * mi->VerticalResolution;

      if (IsSupported(mi) && pixels > max_mode_pixels) {
         max_mode_pixels = pixels;
         max_mode_num = i;
         max_mode_xres = mi->HorizontalResolution;
         max_mode_yres = mi->VerticalResolution;
      }
   }

   if (!my_modes_count) {
      Print(L"No supported modes available\n");
      status = EFI_LOAD_ERROR;
      goto end;
   }

   if (max_mode_num != my_modes[my_modes_count - 1]) {
      Print(L"Mode [%u]: %u x %u%s\n",
            my_modes_count, max_mode_xres,
            max_mode_yres, max_mode_num == default_mode ? L" [DEFAULT]" : L"");
      my_modes[my_modes_count++] = max_mode_num;
   }


   while (true) {

      int my_mode_sel;
      Print(L"Select mode [0-%d] (or ENTER for default): ", my_modes_count - 1);
      k = WaitForKeyPress(ST);

      if (k.UnicodeChar == '\n' || k.UnicodeChar == '\r') {
          wanted_mode = default_mode;
          Print(L"<default>\r\n\r\n");
          break;
      }

      my_mode_sel = k.UnicodeChar - '0';

      if (my_mode_sel < 0 || my_mode_sel >= (int)my_modes_count) {
         Print(L"Invalid selection\n");
         continue;
      }

      wanted_mode = my_modes[my_mode_sel];
      break;
   }

   if (wanted_mode != orig_mode) {

      status = ST->ConOut->ClearScreen(ST->ConOut);
      HANDLE_EFI_ERROR("ClearScreen() failed");

      status = gProt->SetMode(gProt, wanted_mode);

      if (EFI_ERROR(status)) {

         status = gProt->SetMode(gProt, orig_mode);
         status = ST->ConOut->ClearScreen(ST->ConOut);
         HANDLE_EFI_ERROR("ClearScreen() failed");

         Print(L"Loader failed: unable to set desired mode\n");
         status = EFI_LOAD_ERROR;
         goto end;
      }
   }

   *fb_addr = mode->FrameBufferBase;
   *mode_info = *mode->Info;

   PrintModeInfo(mode);

end:
   return status;
}
