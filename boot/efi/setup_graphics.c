/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck/boot/gfx.h>

#include "defs.h"
#include "utils.h"

#include <multiboot.h>

EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt;

static video_mode_t ok_modes[16];
static video_mode_t wanted_mode = INVALID_VIDEO_MODE;

static struct ok_modes_info okm = {
   .ok_modes = ok_modes,
   .ok_modes_array_size = ARRAY_SIZE(ok_modes),
   .ok_modes_cnt = 0,
   .defmode = INVALID_VIDEO_MODE,
};

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

/*
 * Find a good video mode for the bootloader itself.
 *
 * This function is called in EarlySetDefaultResolution(), before displaying
 * anything on the screen. It solves the problem with modern machines with
 * "retina" displays, where just using the native resolution with the default
 * EFI font results in extremely tiny text, a pretty bad user experience.
 */
static EFI_STATUS
FindGoodVideoMode(int bpp, video_mode_t *choice)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi;
   *choice = INVALID_VIDEO_MODE;

   filter_video_modes(NULL,                   /* all_modes */
                      gProt->Mode->MaxMode,   /* all_modes_cnt */
                      &mi,                    /* opaque_mode_info_buf */
                      false,                  /* show_modes */
                      bpp,                    /* bpp */
                      0,                      /* ok_modes_start */
                      &okm);                  /* okm */

   if (okm.ok_modes_cnt)
      *choice = okm.defmode;

   return EFI_SUCCESS;
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
   wanted_mode = origMode;

   status = FindGoodVideoMode(32, &chosenMode);
   HANDLE_EFI_ERROR("FindGoodVideoMode() failed");

   if (chosenMode == INVALID_VIDEO_MODE) {

      /*
       * We were unable to find a good and supported (= 32 bps) video mode.
       * That's bad, but not fatal: just re-run FindGoodVideoMode() including
       * also non-32bps video modes. They are still perfectly fine for the
       * bootloader. The resolution used by Tilck instead, will be chosen later
       * directly by the user, among the available ones.
       */

      status = FindGoodVideoMode(24, &chosenMode);
      HANDLE_EFI_ERROR("FindGoodVideoMode() failed");

      if (chosenMode == INVALID_VIDEO_MODE) {
         /* Do nothing: just keep the current video mode */
         goto end;
      }
   }

   if (chosenMode != origMode) {

      status = gProt->SetMode(gProt, chosenMode);

      if (EFI_ERROR(status)) {
         /* Something went wrong: just restore the previous video mode */
         status = gProt->SetMode(gProt, origMode);
         HANDLE_EFI_ERROR("SetMode() failed");
      }
   }

   wanted_mode = chosenMode;

end:
   return status;
}

static void
PrintFailedModeInfo(UINTN failed_mode)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
   UINTN sizeof_info = 0;
   EFI_STATUS status;

   status = gProt->QueryMode(gProt, failed_mode, &sizeof_info, &mi);

   if (!EFI_ERROR(status)) {
      Print(L"Failed mode info:\n");
      PrintModeInfo(mi);
   } else {
      Print(L"ERROR: Unable to print failed mode info: %r\n", status);
   }
}

bool
SwitchToUserSelectedMode(void)
{
   video_mode_t orig_mode = gProt->Mode->Mode;
   EFI_STATUS status = EFI_SUCCESS;

   if (wanted_mode != orig_mode) {

      ST->ConOut->ClearScreen(ST->ConOut);    /* NOTE: do not handle failures */
      status = gProt->SetMode(gProt, wanted_mode);

      if (EFI_ERROR(status)) {

         gProt->SetMode(gProt, orig_mode);    /* NOTE: do not handle failures */
         ST->ConOut->ClearScreen(ST->ConOut); /* NOTE: do not handle failures */

         Print(L"ERROR: Unable to set desired mode: %r\n", status);
         PrintFailedModeInfo(wanted_mode);
      }
   }

   return status == EFI_SUCCESS;
}

void
AskUserToChooseVideoMode(void)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi;

   filter_video_modes(NULL,                   /* all_modes */
                      gProt->Mode->MaxMode,   /* all_modes_cnt */
                      &mi,                    /* opaque_mode_info_buf */
                      true,                   /* show_modes */
                      32,                     /* bpp */
                      0,                      /* ok_modes_start */
                      &okm);                  /* okm */

   wanted_mode = get_user_video_mode_choice(&okm);
}
