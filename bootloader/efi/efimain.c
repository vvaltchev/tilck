
#include <common/basic_defs.h>
#include <common/failsafe_assert.h>
#include <common/fat32_base.h>

/* We HAVE to undef our ASSERT because the gnu-efi headers define it */
#undef ASSERT

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

#define PAGE_SIZE         4096
#define TEMP_KERNEL_ADDR  (KERNEL_PADDR + KERNEL_MAX_SIZE * 4)
#define KERNEL_FILE       L"\\EFI\\BOOT\\elf_kernel_stripped"


EFI_STATUS SetupGraphicMode(EFI_BOOT_SERVICES *BS, UINTN *xres, UINTN *yres);
void SetMbiFramebufferInfo(multiboot_info_t *mbi, u32 xres, u32 yres);

EFI_STATUS
LoadFileFromDisk(EFI_BOOT_SERVICES *BS,
                 EFI_FILE_PROTOCOL *fileProt,
                 INTN pagesCount,
                 EFI_PHYSICAL_ADDRESS paddr,
                 CHAR16 *filePath)
{
   EFI_STATUS status = EFI_SUCCESS;
   EFI_FILE_PROTOCOL *fileHandle;
   UINTN bufSize = pagesCount * PAGE_SIZE;
   UINT32 crc32 = 0;

   BS->AllocatePages(AllocateAddress, EfiLoaderData, pagesCount, &paddr);
   HANDLE_EFI_ERROR("AllocatePages");

   Print(L"File Open('%s')...\r\n", filePath);
   status =
      fileProt->Open(fileProt, &fileHandle, filePath, EFI_FILE_MODE_READ, 0);
   HANDLE_EFI_ERROR("fileProt->Open");

   Print(L"File Read()...\r\n");
   status = fileProt->Read(fileHandle, &bufSize, (void *)(UINTN)paddr);
   HANDLE_EFI_ERROR("fileProt->Read");

   Print(L"Size read: %d\r\n", bufSize);

   BS->CalculateCrc32((void*)(UINTN)paddr, bufSize, &crc32);
   Print(L"Crc32: 0x%x\r\n", crc32);

   status = fileHandle->Close(fileHandle);
   HANDLE_EFI_ERROR("fileHandle->Close");

end:
   return status;
}

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
      if (phdr->p_type != PT_LOAD) {
         continue;
      }

      CHECK(phdr->p_vaddr >= KERNEL_BASE_VA);
      CHECK(phdr->p_paddr >= KERNEL_PADDR);
      CHECK(phdr->p_paddr < KERNEL_PADDR + KERNEL_MAX_SIZE);

      bzero((void *)(UINTN)phdr->p_paddr, phdr->p_memsz);

      my_memmove((void *)(UINTN)phdr->p_paddr,
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

   Print(L"ELF kernel loaded. Entry: 0x%x\n", *entry);
   status = EFI_SUCCESS;

end:
   return status;
}

EFI_STATUS
LoadRamdisk(EFI_HANDLE image,
            EFI_LOADED_IMAGE *loaded_image,
            EFI_PHYSICAL_ADDRESS *ramdisk_paddr_ref)
{
   EFI_STATUS status = EFI_SUCCESS;
   EFI_BLOCK_IO_PROTOCOL *blockio;
   EFI_DISK_IO_PROTOCOL *ioprot;

   status = BS->OpenProtocol(loaded_image->DeviceHandle,
                             &BlockIoProtocol,
                             (void **)&blockio,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("Getting a BlockIoProtocol handle");

   status = BS->OpenProtocol(loaded_image->DeviceHandle,
                             &DiskIoProtocol,
                             (void **)&ioprot,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("Getting a DiskIOProtocol handle");

   Print(L"Loading ramdisk...\r\n");

   status = BS->AllocatePages(AllocateAnyPages,
                              EfiLoaderData,
                              RAMDISK_SIZE / PAGE_SIZE,
                              ramdisk_paddr_ref);
   HANDLE_EFI_ERROR("AllocatePages");
   //Print(L"RAMDISK paddr: 0x%lx\r\n", *ramdisk_paddr_ref);

   void *fat_hdr = (void *)(UINTN)*ramdisk_paddr_ref;

   status = ioprot->ReadDisk(ioprot,
                             blockio->Media->MediaId,
                             0, // offset from the beginnig of the partition!
                             1 * KB, /* just the header */
                             fat_hdr);
   HANDLE_EFI_ERROR("ReadDisk");


   u32 sector_size = fat_get_sector_size(fat_hdr);
   u32 sectors_per_fat = fat_get_FATSz(fat_hdr);
   u32 total_fat_size = sectors_per_fat * sector_size;
   u32 total_used_bytes;

   status = ioprot->ReadDisk(ioprot,
                             blockio->Media->MediaId,
                             0,
                             total_fat_size, /* only the FAT table */
                             fat_hdr);
   HANDLE_EFI_ERROR("ReadDisk");

   total_used_bytes = fat_get_used_bytes(fat_hdr);
   Print(L"RAMDISK used bytes: %u\n", total_used_bytes);

   status = ioprot->ReadDisk(ioprot,
                             blockio->Media->MediaId,
                             0,
                             total_used_bytes,
                             fat_hdr);
   HANDLE_EFI_ERROR("ReadDisk");

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

   EFI_PHYSICAL_ADDRESS ramdisk_paddr;

   UINTN bufSize;
   void *kernel_entry = NULL;
   multiboot_info_t *mbi;
   multiboot_module_t *mod;

   UINTN xres, yres;

   InitializeLib(image, ST);

   Print(L"--- exOS bootloader ---\r\n");

   status = SetupGraphicMode(BS, &xres, &yres);
   HANDLE_EFI_ERROR("SetupGraphicMode() failed");

   status = BS->OpenProtocol(image,
                             &LoadedImageProtocol,
                             (void**) &loaded_image,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("Getting a LoadedImageProtocol handle");

   status = LoadRamdisk(image, loaded_image, &ramdisk_paddr);
   HANDLE_EFI_ERROR("LoadRamdisk failed");

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

   // Prepare for the actual boot
   // Print(L"Press ANY key to boot the kernel...\r\n");
   // WaitForKeyPress(ST);

   EFI_MEMORY_DESCRIPTOR mmap[128];
   UINTN mmap_size, mapkey, desc_size;
   UINT32 desc_ver;

   mmap_size = sizeof(mmap);

   status = BS->GetMemoryMap(&mmap_size, mmap, &mapkey, &desc_size, &desc_ver);
   HANDLE_EFI_ERROR("BS->GetMemoryMap");

   status = BS->ExitBootServices(image, mapkey);
   HANDLE_EFI_ERROR("BS->ExitBootServices");


   my_memmove((void *)RAMDISK_PADDR,
               (void *)(UINTN)ramdisk_paddr,
               RAMDISK_SIZE);

   ramdisk_paddr = RAMDISK_PADDR;


   /*
    * Setup the multiboot info.
    * Note: we have to do this here, after calling ExitBootServices() because
    * that call wipes out all the data segments.
    */

   mbi = (multiboot_info_t *)TEMP_KERNEL_ADDR;
   bzero(mbi, sizeof(*mbi));

   mod = (multiboot_module_t *)(TEMP_KERNEL_ADDR + sizeof(*mbi));
   bzero(mod, sizeof(*mod));

   SetMbiFramebufferInfo(mbi, xres, yres);
   mbi->mem_upper = 127*1024; /* temp hack */

   mbi->flags |= MULTIBOOT_INFO_MODS;
   mbi->mods_addr = (UINTN)mod;
   mbi->mods_count = 1;
   mod->mod_start = ramdisk_paddr;
   mod->mod_end = mod->mod_start + RAMDISK_SIZE;

   jump_to_kernel(mbi, kernel_entry);

end:
   /* --- we should never get here in the normal case --- */
   WaitForKeyPress(ST);
   return EFI_SUCCESS;
}


