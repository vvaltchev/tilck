/* SPDX-License-Identifier: BSD-2-Clause */

#define __EFI_MAIN__

#include <tilck_gen_headers/config_boot.h>
#include <tilck_gen_headers/config_kernel.h>
#include <tilck_gen_headers/mod_console.h>
#include <tilck_gen_headers/mod_serial.h>
#include <tilck_gen_headers/mod_fb.h>

#include "defs.h"
#include "utils.h"

#include <tilck/boot/common.h>

/**
 * efi_main - The entry point for the EFI application
 * @image: firmware-allocated handle that identifies the image
 * @ST: EFI system table
 */
EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *__ST)
{
   EFI_STATUS status;
   void *kernel_entry;
   UINTN mapkey;

   init_common_bootloader_code(&efi_boot_intf);
   InitializeLib(image, __ST);
   gImageHandle = image;

   status = BS->OpenProtocol(image,
                             &LoadedImageProtocol,
                             (void**) &gLoadedImage,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("OpenProtocol(LoadedImageProtocol)");

   //
   // For debugging with GDB (see docs/debugging.md)
   //

   if (EFI_BOOTLOADER_DEBUG) {
      Print(L"\n");
      Print(L"------------ EFI BOOTLOADER DEBUG ------------\n");
      Print(L"Pointer size:  %d\n", sizeof(void *));
      Print(L"JumpToKernel:  0x%x\n", (void *)JumpToKernel);
      Print(L"BaseAddr:      0x%x\n", gLoadedImage->ImageBase + 0x1000);
      Print(L"Press ANY key to continue...");
      WaitForKeyPress();
   }

   EarlySetDefaultResolution();
   ST->ConOut->EnableCursor(ST->ConOut, true);
   write_bootloader_hello_msg();

   status = BS->OpenProtocol(gLoadedImage->DeviceHandle,
                             &FileSystemProtocol,
                             (void **)&gFileFsProt,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("OpenProtocol FileSystemProtocol");

   status = gFileFsProt->OpenVolume(gFileFsProt, &gFileProt);
   HANDLE_EFI_ERROR("OpenVolume");

   status = ReserveMemAreaForKernelImage();
   HANDLE_EFI_ERROR("ReserveMemAreaForKernelImage");

   if (!common_bootloader_logic()) {
      status = EFI_ABORTED;
      goto end;
   }

   status = SetupMultibootInfo();
   HANDLE_EFI_ERROR("SetupMultibootInfo");

   status = BS->CloseProtocol(gLoadedImage->DeviceHandle,
                              &FileSystemProtocol,
                              image,
                              NULL);
   HANDLE_EFI_ERROR("CloseProtocol(FileSystemProtocol)");
   gFileFsProt = NULL;

   status = BS->CloseProtocol(image, &LoadedImageProtocol, image, NULL);
   HANDLE_EFI_ERROR("CloseProtocol(LoadedImageProtocol)");
   gLoadedImage = NULL;

   status = MultibootSaveMemoryMap(&mapkey);
   HANDLE_EFI_ERROR("MultibootSaveMemoryMap");

   status = KernelLoadMemoryChecks();
   HANDLE_EFI_ERROR("KernelLoadMemoryChecks");

   gExitBootServicesCalled = true;
   status = BS->ExitBootServices(image, mapkey);
   HANDLE_EFI_ERROR("BS->ExitBootServices");

   /* --- Point of no return: from here on, we MUST NOT fail --- */

   kernel_entry = load_kernel_image();
   JumpToKernel(kernel_entry);

end:
   /* --- we should never get here in the normal case --- */
   WaitForKeyPress();
   return status;
}


