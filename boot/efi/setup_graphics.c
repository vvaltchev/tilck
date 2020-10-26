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

EFI_STATUS
EarlySetDefaultResolution(void)
{
   static EFI_HANDLE handles[32];      /* static: reduce stack usage */

   EFI_STATUS status;
   UINTN handles_buf_size;
   UINTN handles_count;
   video_mode_t origMode;
   video_mode_t chosenMode = INVALID_VIDEO_MODE;

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

   origMode = gProt->Mode->Mode;
   chosenMode = find_default_video_mode();

   if (chosenMode != origMode) {

      status = gProt->SetMode(gProt, chosenMode);

      if (EFI_ERROR(status)) {
         /* Something went wrong: just restore the previous video mode */
         status = gProt->SetMode(gProt, origMode);
         HANDLE_EFI_ERROR("SetMode() failed");
      }
   }

end:
   return status;
}
