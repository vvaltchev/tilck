
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

UINTN saved_fb_addr;
UINTN saved_fb_size;
EFI_GRAPHICS_OUTPUT_MODE_INFORMATION saved_mode_info;

void SetMbiFramebufferInfo(multiboot_info_t *mbi, u32 xres, u32 yres)
{
   mbi->flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;
   mbi->framebuffer_addr = saved_fb_addr;
   mbi->framebuffer_pitch =
      saved_mode_info.PixelsPerScanLine * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
   mbi->framebuffer_width = xres;
   mbi->framebuffer_height = yres;
   mbi->framebuffer_bpp = sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) * 8;
   mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
}

void save_mode_info(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode)
{
   saved_mode_info = *mode->Info;
   saved_fb_addr = mode->FrameBufferBase;
   saved_fb_size = mode->FrameBufferSize;
}

void print_mode_info(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode)
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

   Print(L"PixelsPerScanLine: %u\n", saved_mode_info.PixelsPerScanLine);
}

bool is_supported(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi)
{
   if (sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) != 4)
      return false;

   return mi->PixelFormat == PixelBlueGreenRedReserved8BitPerColor;
}

bool is_mode_known(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi)
{
   if (mi->HorizontalResolution == 640 && mi->VerticalResolution == 480)
      return is_supported(mi);

   if (mi->HorizontalResolution == 800 && mi->VerticalResolution == 600)
      return is_supported(mi);

   if (mi->HorizontalResolution == 1024 && mi->VerticalResolution == 768)
      return is_supported(mi);

   if (mi->HorizontalResolution == 1280 && mi->VerticalResolution == 1024)
      return is_supported(mi);

   if (mi->HorizontalResolution == 1920 && mi->VerticalResolution == 1080)
      return is_supported(mi);

   return false;
}

bool is_exos_default_mode(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi)
{
   if (is_supported(mi))
      if (mi->HorizontalResolution == 800 && mi->VerticalResolution == 600)
         return true;

   return false;
}

EFI_STATUS
SetupGraphicMode(EFI_BOOT_SERVICES *BS, UINTN *xres, UINTN *yres)
{
   UINTN status = EFI_SUCCESS;

   EFI_HANDLE handles[32];
   UINTN handles_buf_size;
   UINTN handles_count;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt;
   EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
   EFI_INPUT_KEY k;

   UINTN wanted_mode;
   UINTN orig_mode;
   UINTN default_mode;

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

      if (is_exos_default_mode(mi)) {
         default_mode = i;
      }

      if (is_mode_known(mi) || (is_supported(mi) && i == mode->MaxMode - 1)) {
         Print(L"Mode [%u]: %u x %u%s\n",
               my_modes_count,
               mi->HorizontalResolution,
               mi->VerticalResolution,
               i == default_mode ? L" [DEFAULT]" : L"");

         my_modes[my_modes_count++] = i;
      }

      u32 pixels = mi->HorizontalResolution * mi->VerticalResolution;

      if (is_supported(mi) && pixels > max_mode_pixels) {
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

   int my_mode_sel;

   while (true) {

      Print(L"Select mode [0-%d] (or ENTER for default): ", my_modes_count - 1);
      k = WaitForKeyPress(ST);

      if (k.UnicodeChar == '\n' || k.UnicodeChar == '\r') {
          wanted_mode = default_mode;
          //Print(L"[CURRENT]\n");
          break;
      }

      my_mode_sel = k.UnicodeChar - '0';

      if (my_mode_sel < 0 || my_mode_sel >= my_modes_count) {
         Print(L"Invalid selection\n");
         continue;
      }

      wanted_mode = my_modes[my_mode_sel];
      //Print(L"%d\n", my_mode_sel);
      break;
   }

   //Print(L"About to switch the video mode. Press any key to continue.\n");
   //WaitForKeyPress(ST);

   status = ST->ConOut->ClearScreen(ST->ConOut);
   HANDLE_EFI_ERROR("ClearScreen() failed");

   if (wanted_mode != orig_mode) {

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

   save_mode_info(mode);
   print_mode_info(mode);

   *xres = mode->Info->HorizontalResolution;
   *yres = mode->Info->VerticalResolution;

end:
   return status;
}
