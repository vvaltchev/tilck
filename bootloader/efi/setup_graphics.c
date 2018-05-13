
#include <efi.h>
#include <efilib.h>
#include <multiboot.h>

#include "efibind.h"
#include "efidef.h"
#include "efidevp.h"
#include "eficon.h"
#include "efiapi.h"
#include "efierr.h"
#include "efiprot.h"

#include "utils.h"

#define DESIRED_RES_X 800
#define DESIRED_RES_Y 600

UINTN saved_fb_addr;
UINTN saved_fb_size;
EFI_GRAPHICS_OUTPUT_MODE_INFORMATION saved_mode_info;

void SetMbiFramebufferInfo(multiboot_info_t *mbi)
{
   mbi->flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;
   mbi->framebuffer_addr = saved_fb_addr;
   mbi->framebuffer_pitch =
      saved_mode_info.PixelsPerScanLine * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
   mbi->framebuffer_width = DESIRED_RES_X;
   mbi->framebuffer_height = DESIRED_RES_Y;
   mbi->framebuffer_bpp = sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) * 8;
   mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
}

void print_mode_info(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode)
{
   saved_mode_info = *mode->Info;
   saved_fb_addr = mode->FrameBufferBase;
   saved_fb_size = mode->FrameBufferSize;

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

   Print(L"PixelsPerScanLine: %u\n", saved_mode_info.PixelsPerScanLine);
}

bool is_pixelformat_supported(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info)
{
   if (sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) != 4)
      return false;

   return mode_info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ||
          mode_info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor;
}

EFI_STATUS
SetupGraphicMode(EFI_BOOT_SERVICES *BS)
{
   UINTN status = EFI_SUCCESS;

   EFI_HANDLE handles[32];
   UINTN handles_buf_size;
   UINTN handles_count;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt;

   handles_buf_size = sizeof(handles);

   status = BS->LocateHandle(ByProtocol,
                             &GraphicsOutputProtocol,
                             NULL,
                             &handles_buf_size,
                             handles);

   HANDLE_EFI_ERROR("LocateHandle() failed");

   handles_count = handles_buf_size/sizeof(EFI_HANDLE);

   CHECK(handles_count > 0);

   status = BS->HandleProtocol(handles[0],
                               &GraphicsOutputProtocol,
                               (void **)&gProt);

   HANDLE_EFI_ERROR("HandleProtocol() failed");

   EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode = gProt->Mode;

   // Debug: display current mode before changing it.
   // print_mode_info(mode);

   UINTN wanted_mode = (UINTN)-1;
   UINTN orig_mode = mode->Mode;

   for (UINTN i = 0; i < mode->MaxMode; i++) {

      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
      UINTN sizeof_info = 0;

      status = gProt->QueryMode(gProt, i, &sizeof_info, &mi);
      HANDLE_EFI_ERROR("QueryMode() failed");

      // Print(L"Mode [%u]: %u x %u\n",
      //       i,
      //       mi->HorizontalResolution,
      //       mi->VerticalResolution);

      if (mi->HorizontalResolution == DESIRED_RES_X &&
          mi->VerticalResolution == DESIRED_RES_Y &&
          is_pixelformat_supported(mi)) {

         wanted_mode = i;
         break;
      }
   }

   if (wanted_mode == (UINTN)-1) {
      Print(L"Wanted mode %u x %u NOT AVAILABLE.\n",
            DESIRED_RES_X, DESIRED_RES_Y);
      status = EFI_LOAD_ERROR;
      goto end;
   }

   // Print(L"About to switch to mode %u [%u x %u]. Press any key\n",
   //       wanted_mode, DESIRED_RES_X, DESIRED_RES_Y);
   // WaitForKeyPress(ST);

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

   print_mode_info(mode);

end:
   return status;
}
