
#include "defs.h"
#include "utils.h"


/**
 * efi_main - The entry point for the EFI application
 * @image: firmware-allocated handle that identifies the image
 * @ST: EFI system table
 */
EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *ST)
{
   EFI_STATUS status;
   EFI_LOADED_IMAGE *loaded_image;
   EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fileFsProt;
   EFI_FILE_PROTOCOL *fileProt;
   EFI_FILE_PROTOCOL *fileHandle;
   EFI_PHYSICAL_ADDRESS ramdisk_paddr;
   UINTN ramdisk_size, mapkey, fb_paddr;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION gfx_mode_info;
   EFI_BOOT_SERVICES *BS = ST->BootServices;
   void *kernel_entry = NULL;

   InitializeLib(image, ST);

   Print(L"----- Hello from exOS's UEFI bootloader! -----\r\n\r\n");

   status = SetupGraphicMode(BS, &fb_paddr, &gfx_mode_info);

   HANDLE_EFI_ERROR("SetupGraphicMode() failed");

   status = BS->OpenProtocol(image,
                             &LoadedImageProtocol,
                             (void**) &loaded_image,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("Getting a LoadedImageProtocol handle");


   // ------------------------------------------------------------------ //

   Print(L"OpenProtocol (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL)...\r\n");
   status = BS->OpenProtocol(loaded_image->DeviceHandle,
                             &FileSystemProtocol,
                             (void **)&fileFsProt,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("OpenProtocol FileSystemProtocol");

   Print(L"OpenVolume()...\r\n");
   status = fileFsProt->OpenVolume(fileFsProt, &fileProt);
   HANDLE_EFI_ERROR("OpenVolume");

   status = LoadElfKernel(BS, fileProt, &kernel_entry);
   HANDLE_EFI_ERROR("LoadElfKernel");

   // ------------------------------------------------------------------ //

   status = LoadRamdisk(image, loaded_image, &ramdisk_paddr, &ramdisk_size);
   HANDLE_EFI_ERROR("LoadRamdisk failed");

   status = AllocateMbi();
   HANDLE_EFI_ERROR("AllocateMbi");

   status = MbiSetFramebufferInfo(&gfx_mode_info, fb_paddr);
   HANDLE_EFI_ERROR("MbiSetFramebufferInfo");

   status = MbiSetRamdisk(ramdisk_paddr, ramdisk_size);
   HANDLE_EFI_ERROR("MbiSetRamdisk");

   // Print(L"Press ANY key to boot the kernel...\r\n");
   // WaitForKeyPress(ST);

   status = MultibootSaveMemoryMap(&mapkey);
   HANDLE_EFI_ERROR("MultibootSaveMemoryMap");

   status = BS->ExitBootServices(image, mapkey);
   HANDLE_EFI_ERROR("BS->ExitBootServices");

   JumpToKernel(mbi, kernel_entry);

end:
   /* --- we should never get here in the normal case --- */
   WaitForKeyPress(ST);
   return EFI_SUCCESS;
}


