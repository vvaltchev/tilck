
#include <exos/common/basic_defs.h>

#include <multiboot.h>
#include <elf.h>

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
#include "multiboot_funcs.h"

#define TEMP_KERNEL_ADDR  (KERNEL_PADDR + KERNEL_MAX_SIZE)

#define KERNEL_FILE CONCAT(L, KERNEL_FILE_PATH_EFI)

EFI_STATUS
LoadRamdisk(EFI_HANDLE image,
            EFI_LOADED_IMAGE *loaded_image,
            EFI_PHYSICAL_ADDRESS *ramdisk_paddr_ref,
            UINTN *ramdisk_size);

EFI_STATUS
SetupGraphicMode(EFI_BOOT_SERVICES *BS,
                 UINTN *fb_addr /* out */,
                 EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info /* out */);


EFI_STATUS
LoadElfKernel(EFI_BOOT_SERVICES *BS,
              EFI_FILE_PROTOCOL *fileProt,
              void **entry)
{
   EFI_STATUS status = EFI_LOAD_ERROR;
   EFI_PHYSICAL_ADDRESS kernel_paddr = KERNEL_PADDR;

   /*
    * Temporary load the whole kernel file in a safe location.
    */
   status = LoadFileFromDisk(BS, fileProt, KERNEL_MAX_SIZE / PAGE_SIZE,
                             TEMP_KERNEL_ADDR, KERNEL_FILE);
   HANDLE_EFI_ERROR("LoadFileFromDisk");

   Print(L"Kernel loaded in temporary paddr.\n");
   Print(L"Allocating memory for final kernel's location...\n");

   status = BS->AllocatePages(AllocateAddress,
                              EfiLoaderData,
                              KERNEL_MAX_SIZE / PAGE_SIZE,
                              &kernel_paddr);

   HANDLE_EFI_ERROR("AllocatePages");
   Print(L"Memory allocated.\n");

   CHECK(kernel_paddr == KERNEL_PADDR);

   Elf32_Ehdr *header = (Elf32_Ehdr *)TEMP_KERNEL_ADDR;

   CHECK(header->e_ident[EI_MAG0] == ELFMAG0);
   CHECK(header->e_ident[EI_MAG1] == ELFMAG1);
   CHECK(header->e_ident[EI_MAG2] == ELFMAG2);
   CHECK(header->e_ident[EI_MAG3] == ELFMAG3);
   CHECK(header->e_ehsize == sizeof(*header));

   Elf32_Phdr *phdr = (Elf32_Phdr *)(header + 1);

   Print(L"Num of program headers: %d\n", header->e_phnum);

   for (int i = 0; i < header->e_phnum; i++, phdr++) {

      // Ignore non-load segments.
      if (phdr->p_type != PT_LOAD)
         continue;

      CHECK(phdr->p_vaddr >= KERNEL_BASE_VA);
      CHECK(phdr->p_paddr >= KERNEL_PADDR);
      CHECK(phdr->p_paddr < KERNEL_PADDR + KERNEL_MAX_SIZE);

      BS->SetMem((void *)(UINTN)phdr->p_paddr, phdr->p_memsz, 0);

      BS->CopyMem((void *)(UINTN)phdr->p_paddr,
                  (char *) header + phdr->p_offset,
                  phdr->p_filesz);

      if (IN_RANGE(header->e_entry,
                   phdr->p_vaddr,
                   phdr->p_vaddr + phdr->p_filesz)) {
         /*
          * If e_entry is a vaddr (address >= KERNEL_BASE_VA), we need to
          * calculate its paddr because here paging is OFF. Therefore,
          * compute its offset from the beginning of the segment and add it
          * to the paddr of the segment.
          */

         *entry =
            (void *)(UINTN)(phdr->p_paddr + (header->e_entry - phdr->p_vaddr));
      }
   }

   status = BS->FreePages(TEMP_KERNEL_ADDR, KERNEL_MAX_SIZE / PAGE_SIZE);
   HANDLE_EFI_ERROR("FreePages");

   Print(L"ELF kernel loaded. Entry: 0x%x\n", *entry);
   status = EFI_SUCCESS;

end:
   return status;
}

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
   EFI_BOOT_SERVICES *BS = ST->BootServices;

   UINTN saved_fb_addr;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION saved_mode_info;

   EFI_PHYSICAL_ADDRESS ramdisk_paddr;
   UINTN ramdisk_size;
   //UINTN xres, yres;
   UINTN bufSize;

   void *kernel_entry = NULL;

   InitializeLib(image, ST);

   Print(L"----- Hello from exOS's UEFI bootloader! -----\r\n\r\n");

   status = SetupGraphicMode(BS, &saved_fb_addr, &saved_mode_info);

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

   status = MbiSetFramebufferInfo(&saved_mode_info, saved_fb_addr);
   HANDLE_EFI_ERROR("MbiSetFramebufferInfo");

   status = MbiSetRamdisk(ramdisk_paddr, ramdisk_size);
   HANDLE_EFI_ERROR("MbiSetRamdisk");

   // Prepare for the actual boot
   // Print(L"mbi buffer: 0x%x\n", multiboot_buffer);
   // Print(L"RAMDISK paddr: 0x%x\n", ramdisk_paddr);
   // Print(L"RAMDISK size: %u\n", ramdisk_size);

   // Print(L"Press ANY key to boot the kernel...\r\n");
   // WaitForKeyPress(ST);

   UINTN mapkey;
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


