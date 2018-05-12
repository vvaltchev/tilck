
#include <efi.h>
#include <efilib.h>

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

void draw_pixel(int x, int y, UINTN color)
{
   const UINTN PixelElementSize = sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
   const UINTN PixelsPerScanLine = saved_mode_info.PixelsPerScanLine;

   UINTN addr = saved_fb_addr +
                ( (PixelsPerScanLine * y) + x ) * PixelElementSize;
   *(volatile UINTN *)(addr) = color;
}

UINTN my_make_color(int r, int g, int b)
{
   return (r << 16) | (g << 8) | b;
}

void draw_something(void)
{
   UINTN white_val = my_make_color(255, 255, 255);
   UINTN red_val = my_make_color(255, 0, 0);
   UINTN green_val = my_make_color(0, 255, 0);
   UINTN blue_val = my_make_color(0, 0, 255);

   int iy = 20;
   int ix = 600;
   int w = 200;

   for (int y = iy; y < iy+10; y++)
      for (int x = ix; x < ix+w; x++)
         draw_pixel(x, y, red_val);

   iy+=20;

   for (int y = iy; y < iy+10; y++)
      for (int x = ix; x < ix+w; x++)
         draw_pixel(x, y, white_val);

   iy+=20;

   for (int y = iy; y < iy+10; y++)
      for (int x = ix; x < ix+w; x++)
         draw_pixel(x, y, green_val);

   iy+=20;

   for (int y = iy; y < iy+10; y++)
      for (int x = ix; x < ix+w; x++)
         draw_pixel(x, y, blue_val);
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
   EFI_GRAPHICS_OUTPUT_PROTOCOL *graphicsProtocol;

   handles_buf_size = sizeof(handles);

   status = uefi_call_wrapper(BS->LocateHandle,
                              5,
                              ByProtocol,
                              &GraphicsOutputProtocol,
                              NULL,
                              &handles_buf_size,
                              handles);

   HANDLE_EFI_ERROR("LocateHandle() failed");

   handles_count = handles_buf_size/sizeof(EFI_HANDLE);

   CHECK(handles_count > 0);

   status = uefi_call_wrapper(BS->HandleProtocol,
                              3,
                              handles[0],
                              &GraphicsOutputProtocol,
                              (void **)&graphicsProtocol);

   HANDLE_EFI_ERROR("HandleProtocol() failed");

   EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode = graphicsProtocol->Mode;

   print_mode_info(mode);

   UINTN wanted_mode = (UINTN)-1;
   UINTN orig_mode = mode->Mode;

   for (UINTN i = 0; i < mode->MaxMode; i++) {

      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info = NULL;
      UINTN sizeof_info = 0;

      status = uefi_call_wrapper(graphicsProtocol->QueryMode,
                                 4,
                                 graphicsProtocol,
                                 i,
                                 &sizeof_info,
                                 &mode_info);

      HANDLE_EFI_ERROR("QueryMode() failed");

      // Print(L"Mode [%u]: %u x %u\n",
      //       i,
      //       mode_info->HorizontalResolution,
      //       mode_info->VerticalResolution);

      if (mode_info->HorizontalResolution == DESIRED_RES_X &&
          mode_info->VerticalResolution == DESIRED_RES_Y &&
          is_pixelformat_supported(mode_info)) {

         wanted_mode = i;
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

   status = uefi_call_wrapper(ST->ConOut->ClearScreen,
                              1,
                              ST->ConOut);

   HANDLE_EFI_ERROR("ClearScreen() failed");

   status = uefi_call_wrapper(graphicsProtocol->SetMode,
                              2,
                              graphicsProtocol,
                              wanted_mode);

   if (EFI_ERROR(status)) {
      status = uefi_call_wrapper(graphicsProtocol->SetMode,
                                 2,
                                 graphicsProtocol,
                                 orig_mode);

      status = uefi_call_wrapper(ST->ConOut->ClearScreen,
                                 1,
                                 ST->ConOut);

      HANDLE_EFI_ERROR("ClearScreen() failed");

      Print(L"Loader failed: unable to set desired mode\n");
      status = EFI_LOAD_ERROR;
   }

   print_mode_info(mode);

end:
   return status;
}
