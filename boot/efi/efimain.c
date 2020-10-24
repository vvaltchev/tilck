/* SPDX-License-Identifier: BSD-2-Clause */

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
   EFI_LOADED_IMAGE *loaded_image;
   EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fileFsProt;
   EFI_FILE_PROTOCOL *fileProt;
   EFI_PHYSICAL_ADDRESS ramdisk_paddr;
   EFI_PHYSICAL_ADDRESS kernel_file_paddr;
   UINTN ramdisk_size, mapkey;
   void *kernel_entry = NULL;

   init_common_bootloader_code(&efi_boot_intf);
   InitializeLib(image, __ST);
   gImageHandle = image;

   EarlySetDefaultResolution();
   ST->ConOut->EnableCursor(ST->ConOut, true);

   Print(L"----- Hello from Tilck's UEFI bootloader! -----\n\n");

   status = BS->OpenProtocol(image,
                             &LoadedImageProtocol,
                             (void**) &loaded_image,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("OpenProtocol(LoadedImageProtocol)");


   // ------------------------------------------------------------------ //

   status = BS->OpenProtocol(loaded_image->DeviceHandle,
                             &FileSystemProtocol,
                             (void **)&fileFsProt,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("OpenProtocol FileSystemProtocol");

   status = fileFsProt->OpenVolume(fileFsProt, &fileProt);
   HANDLE_EFI_ERROR("OpenVolume");

   status = LoadKernelFile(fileProt, &kernel_file_paddr);
   HANDLE_EFI_ERROR("LoadKernelFile");

   status = BS->CloseProtocol(loaded_image->DeviceHandle,
                              &FileSystemProtocol,
                              image,
                              NULL);
   HANDLE_EFI_ERROR("CloseProtocol(FileSystemProtocol)");
   fileFsProt = NULL;

   // ------------------------------------------------------------------ //

   status = LoadRamdisk(image,
                        loaded_image,
                        &ramdisk_paddr,
                        &ramdisk_size,
                        2); /* CurrConsoleRow (HACK). See ShowProgress() */

   HANDLE_EFI_ERROR("LoadRamdisk failed");
   Print(L"\n");

   status = AllocateMbi();
   HANDLE_EFI_ERROR("AllocateMbi");

   status = MbiSetRamdisk(ramdisk_paddr, ramdisk_size);
   HANDLE_EFI_ERROR("MbiSetRamdisk");

   status = MbiSetBootloaderName();
   HANDLE_EFI_ERROR("MbiSetBootloaderName");

   status = MbiSetPointerToAcpiTable();
   HANDLE_EFI_ERROR("MbiSetPointerToAcpiTable");

   if (MOD_console && MOD_fb) {

      if (BOOT_INTERACTIVE) {
         do {

            AskUserToChooseVideoMode();

         } while (!SwitchToUserSelectedMode());
      }

      MbiSetFramebufferInfo(gProt->Mode->Info, gProt->Mode->FrameBufferBase);
   }

   //
   // For debugging with GDB (see docs/efi_debug.md)
   //
   // Print(L"Pointer size: %d\n", sizeof(void *));
   // Print(L"JumpToKernel: 0x%x\n", (void *)JumpToKernel);
   // Print(L"BaseAddr: 0x%x\n", loaded_image->ImageBase + 0x1000);
   // Print(L"Press ANY key to boot the kernel...\n");
   // WaitForKeyPress();
   //

   status = BS->CloseProtocol(image, &LoadedImageProtocol, image, NULL);
   HANDLE_EFI_ERROR("CloseProtocol(LoadedImageProtocol)");
   loaded_image = NULL;

   if (!(MOD_console && MOD_fb)) {

      Print(L"WARNING: MOD_fb=0, Tilck won't support graphics mode.\n");
      Print(L"WARNING: text mode is NOT available with UEFI boot.\n\n");

      if (MOD_serial && (TINY_KERNEL || SERIAL_CON_IN_VIDEO_MODE)) {

         Print(L"Only the serial console is available. Use it.\n\n");
         Print(L"Press ANY key to boot");
         WaitForKeyPress();

         ST->ConOut->ClearScreen(ST->ConOut);
         Print(L"<No video console>");

      } else {

         Print(L"ERROR: No serial console enabled. Refuse to boot.\n");
         status = EFI_ABORTED;
         goto end;
      }
   }

   status = MultibootSaveMemoryMap(&mapkey);
   HANDLE_EFI_ERROR("MultibootSaveMemoryMap");

   status = KernelLoadMemoryChecks();
   HANDLE_EFI_ERROR("KernelLoadMemoryChecks");

   status = BS->ExitBootServices(image, mapkey);
   HANDLE_EFI_ERROR("BS->ExitBootServices");

   /* --- Point of no return: from here on, we MUST NOT fail --- */

   kernel_entry = simple_elf_loader(TO_PTR(kernel_file_paddr));
   JumpToKernel(kernel_entry);

end:
   /* --- we should never get here in the normal case --- */
   WaitForKeyPress();
   return status;
}


