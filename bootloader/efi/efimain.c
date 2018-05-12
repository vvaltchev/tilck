#include <efi.h>
#include <efilib.h>

#include "efibind.h"
#include "efidef.h"
#include "efidevp.h"
#include "eficon.h"
#include "efiapi.h"
#include "efierr.h"
#include "efiprot.h"

#include <elf.h>

#include <common/config.h>
#include <multiboot.h>

#include "utils.h"

#define PAGE_SIZE            0x1000    // 4 KB
#define KERNEL_FILE      L"\\EFI\\BOOT\\elf_kernel_stripped"

void switch_to_pm32_and_jump_to_kernel(void);

EFI_STATUS SetupGraphicMode(EFI_BOOT_SERVICES *BS);
void set_mbi_framebuffer_info(multiboot_info_t *mbi);
void draw_something(void);

const UINTN temp_kernel_addr = KERNEL_PADDR + KERNEL_MAX_SIZE * 4;


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

   Print(L"AllocatePages for '%s'..\r\n", filePath);

   status = uefi_call_wrapper(BS->AllocatePages,
                              4,
                              AllocateAddress,
                              EfiBootServicesData,
                              pagesCount,
                              &paddr);
   HANDLE_EFI_ERROR("AllocatePages");

   Print(L"File Open('%s')...\r\n", filePath);
   status = uefi_call_wrapper(fileProt->Open, 5, fileProt,
                              &fileHandle, filePath,
                              EFI_FILE_MODE_READ, 0);
   HANDLE_EFI_ERROR("fileProt->Open");

   Print(L"File Read()...\r\n");

   status = uefi_call_wrapper(fileProt->Read, 3,
                              fileHandle, &bufSize, (void *)(UINTN)paddr);
   HANDLE_EFI_ERROR("fileProt->Read");


   Print(L"Size read: %d\r\n", bufSize);

   uefi_call_wrapper(BS->CalculateCrc32, 3, (void*)(UINTN)paddr, bufSize, &crc32);
   Print(L"Crc32: 0x%x\r\n", crc32);

   status = uefi_call_wrapper(fileHandle->Close, 1, fileHandle);
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
                             temp_kernel_addr, KERNEL_FILE);
   HANDLE_EFI_ERROR("LoadFileFromDisk");

   Print(L"Kernel loaded in temporary paddr.\n");
   Print(L"Allocating memory for final kernel's location...\n");

   status = uefi_call_wrapper(BS->AllocatePages,
                              4,
                              AllocateAddress,
                              EfiBootServicesData,
                              KERNEL_MAX_SIZE / PAGE_SIZE,
                              &kernel_paddr);
   HANDLE_EFI_ERROR("AllocatePages");
   Print(L"Memory allocated.\n");

   CHECK(kernel_paddr == KERNEL_PADDR);

   Elf32_Ehdr *header = (Elf32_Ehdr *)temp_kernel_addr;

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
   EFI_DEVICE_PATH *device_path;
   EFI_BLOCK_IO_PROTOCOL *blockio;
   EFI_DISK_IO_PROTOCOL *ioprot;
   EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fileFsProt;
   EFI_FILE_PROTOCOL *fileProt;
   EFI_FILE_PROTOCOL *fileHandle;

   UINTN bufSize;
   UINTN crc32;
   void *kernel_entry = NULL;
   EFI_PHYSICAL_ADDRESS ramdisk_paddr = RAMDISK_PADDR;
   EFI_BOOT_SERVICES *BS = ST->BootServices;
   multiboot_info_t *mbi = NULL;

   InitializeLib(image, ST);

   Print(L"--- exOS bootloader ---\r\n");

   SetupGraphicMode(BS);
   HANDLE_EFI_ERROR("SetupGraphicMode() failed");

   status = uefi_call_wrapper(BS->OpenProtocol,
                              6,                                  // #args
                              image,                              // arg 1
                              &LoadedImageProtocol,               // arg 2
                              (void**)&loaded_image,              // arg 3
                              image,                              // arg 4
                              NULL,                               // arg 5
                              EFI_OPEN_PROTOCOL_GET_PROTOCOL);    // arg 6

   HANDLE_EFI_ERROR("Getting a LoadedImageProtocol handle");

   status = uefi_call_wrapper(BS->OpenProtocol,
                              6,
                              loaded_image->DeviceHandle,
                              &BlockIoProtocol,
                              (void **)&blockio,
                              image,
                              NULL,
                              EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("Getting a BlockIoProtocol handle");

   status = uefi_call_wrapper(BS->OpenProtocol, 6,
                              loaded_image->DeviceHandle,
                              &DiskIoProtocol,
                              (void **)&ioprot,
                              image,
                              NULL,
                              EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("Getting a DiskIOProtocol handle");

   Print(L"Loading ramdisk...\r\n");

   status = uefi_call_wrapper(BS->AllocatePages,
                              4,
                              AllocateAnyPages,
                              EfiBootServicesData,
                              RAMDISK_SIZE / PAGE_SIZE,
                              &ramdisk_paddr);

   HANDLE_EFI_ERROR("AllocatePages");

   status = uefi_call_wrapper(ioprot->ReadDisk,
                              5, /* #args */
                              ioprot,
                              blockio->Media->MediaId,
                              0, // offset from the beginnig of the partition!
                              RAMDISK_SIZE /* buffer size */,
                              (void *)(UINTN)ramdisk_paddr);
   HANDLE_EFI_ERROR("ReadDisk");
   Print(L"RAMDISK paddr: 0x%lx\r\n", ramdisk_paddr);

   crc32 = 0;
   uefi_call_wrapper(BS->CalculateCrc32, 3,
                     (void *)(UINTN)ramdisk_paddr, RAMDISK_SIZE, &crc32);
   Print(L"RAMDISK CRC32: 0x%x\r\n", crc32);

   // ------------------------------------------------------------------ //

   Print(L"OpenProtocol (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL)...\r\n");
   status = uefi_call_wrapper(BS->OpenProtocol,
                              6,
                              loaded_image->DeviceHandle,
                              &FileSystemProtocol,
                              (void **)&fileFsProt,
                              image,
                              NULL,
                              EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("OpenProtocol FileSystemProtocol");

   Print(L"OpenVolume()...\r\n");
   status = uefi_call_wrapper(fileFsProt->OpenVolume, 2, fileFsProt, &fileProt);
   HANDLE_EFI_ERROR("OpenVolume");

   status = LoadElfKernel(BS, fileProt, &kernel_entry);
   HANDLE_EFI_ERROR("LoadElfKernel");

   mbi = (multiboot_info_t *)temp_kernel_addr;
   Print(L"MBI: 0x%x\n", (UINTN)mbi);
   Print(L"switch_to_pm32_and_jump_to_kernel: 0x%x\n", &switch_to_pm32_and_jump_to_kernel);

   // Prepare for the actual boot
   Print(L"Press ANY key to boot the kernel...\r\n");

   // debug
   draw_something();

   WaitForKeyPress(ST);


   EFI_MEMORY_DESCRIPTOR mmap[128];
   UINTN mmap_size, mapkey, desc_size, desc_ver;


   mmap_size = sizeof(mmap);

   status = uefi_call_wrapper(BS->GetMemoryMap, 5, &mmap_size,
                              mmap, &mapkey, &desc_size, &desc_ver);
   HANDLE_EFI_ERROR("BS->GetMemoryMap");

   status = uefi_call_wrapper(BS->ExitBootServices, 2, image, mapkey);
   HANDLE_EFI_ERROR("BS->ExitBootServices");

   if (ramdisk_paddr != RAMDISK_PADDR) {
      my_memmove((void *)RAMDISK_PADDR,
                 (void *)(UINTN)ramdisk_paddr,
                  RAMDISK_SIZE);
   }

   /* Setup the multiboot info */
   set_mbi_framebuffer_info(mbi);
   mbi->mem_upper = 127*1024; /* temp hack */

#ifdef BITS64
   /* Jump to the switchmode code */
   asmVolatile("jmp switch_to_pm32_and_jump_to_kernel"
               : /* no output */
               : "a" (MULTIBOOT_BOOTLOADER_MAGIC),
                 "b" (mbi)
               : /* no clobber */);
#else
   /* Jump to the kernel */
   asmVolatile("jmp *%%ecx"
               : /* no output */
               : "a" (MULTIBOOT_BOOTLOADER_MAGIC),
                 "b" (mbi),
                 "c" (kernel_entry)
               : /* no clobber */);
#endif

   /* --- we should never get here in the normal case --- */

end:
   WaitForKeyPress(ST);
   return EFI_SUCCESS;
}


