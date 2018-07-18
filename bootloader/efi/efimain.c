
#include <exos/common/basic_defs.h>
#include <exos/common/failsafe_assert.h>
#include <exos/common/fat32_base.h>

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

#define _CONCAT(a, b) a##b
#define CONCAT(a, b) _CONCAT(a, b)

#define PAGE_SIZE         4096
#define TEMP_KERNEL_ADDR  (KERNEL_PADDR + KERNEL_MAX_SIZE)

#define KERNEL_FILE CONCAT(L, KERNEL_FILE_PATH_EFI)

EFI_STATUS SetupGraphicMode(EFI_BOOT_SERVICES *BS, UINTN *xres, UINTN *yres);
void SetMbiFramebufferInfo(multiboot_info_t *mbi, u32 xres, u32 yres);

EFI_MEMORY_DESCRIPTOR mmap[512];

multiboot_info_t *mbi;
multiboot_memory_map_t *multiboot_mmap;
UINT32 mmap_elems_count = 0;


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

EFI_STATUS
LoadRamdisk(EFI_HANDLE image,
            EFI_LOADED_IMAGE *loaded_image,
            EFI_PHYSICAL_ADDRESS *ramdisk_paddr_ref,
            UINTN *ramdisk_size)
{
   EFI_STATUS status = EFI_SUCCESS;
   EFI_BLOCK_IO_PROTOCOL *blockio;
   EFI_DISK_IO_PROTOCOL *ioprot;

   u32 sector_size;
   u32 sectors_per_fat;
   u32 total_fat_size;
   u32 total_used_bytes;
   void *fat_hdr;

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
                              1, /* just 1 page */
                              ramdisk_paddr_ref);
   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = (void *)(UINTN)*ramdisk_paddr_ref;

   status = ioprot->ReadDisk(ioprot,
                             blockio->Media->MediaId,
                             0, // offset from the beginnig of the partition!
                             1 * KB, /* just the header */
                             fat_hdr);
   HANDLE_EFI_ERROR("ReadDisk");


   sector_size = fat_get_sector_size(fat_hdr);
   sectors_per_fat = fat_get_FATSz(fat_hdr);
   total_fat_size = (fat_get_first_data_sector(fat_hdr) + 1) * sector_size;

   status = BS->FreePages(*ramdisk_paddr_ref, 1);
   HANDLE_EFI_ERROR("FreePages");

   /* Now allocate memory for storing the whole FAT table */

   status = BS->AllocatePages(AllocateAnyPages,
                              EfiLoaderData,
                              (total_fat_size / PAGE_SIZE) + 1,
                              ramdisk_paddr_ref);
   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = (void *)(UINTN)*ramdisk_paddr_ref;

   status = ioprot->ReadDisk(ioprot,
                             blockio->Media->MediaId,
                             0,
                             total_fat_size, /* only the FAT table */
                             fat_hdr);
   HANDLE_EFI_ERROR("ReadDisk");

   total_used_bytes = fat_get_used_bytes(fat_hdr);
   Print(L"RAMDISK used bytes: %u\n", total_used_bytes);

   //*ramdisk_size = fat_get_TotSec(fat_hdr) * sector_size;

   /*
    * Pass via multiboot 'used bytes' as RAMDISK size instead of the real
    * RAMDISK size. This is useful if the kernel uses the RAMDISK read-only.
    */
   *ramdisk_size = total_used_bytes;


   /*
    * Now we know everything. Free the memory used so far and allocate the
    * big buffer to store all the "used" clusters of the FAT32 partition,
    * including clearly the header and the FAT table.
    */

   status = BS->FreePages(*ramdisk_paddr_ref, (total_fat_size / PAGE_SIZE) + 1);
   HANDLE_EFI_ERROR("FreePages");

   *ramdisk_paddr_ref = KERNEL_PADDR + KERNEL_MAX_SIZE;

   status = BS->AllocatePages(AllocateAddress,
                              EfiLoaderData,
                              (total_used_bytes / PAGE_SIZE) + 1,
                              ramdisk_paddr_ref);
   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = (void *)(UINTN)*ramdisk_paddr_ref;

   status = ioprot->ReadDisk(ioprot,
                             blockio->Media->MediaId,
                             0,
                             total_used_bytes,
                             fat_hdr);
   HANDLE_EFI_ERROR("ReadDisk");

end:
   return status;
}


int efi_to_multiboot_mem_type(UINT32 type)
{
   switch (type) {

      case EfiReservedMemoryType:
      case EfiRuntimeServicesCode:
      case EfiRuntimeServicesData:
         return MULTIBOOT_MEMORY_RESERVED;

      case EfiLoaderCode:
      case EfiLoaderData:
      case EfiBootServicesCode:
      case EfiBootServicesData:
      case EfiConventionalMemory:
         return MULTIBOOT_MEMORY_AVAILABLE;

      case EfiUnusableMemory:
         return MULTIBOOT_MEMORY_BADRAM;

      case EfiACPIReclaimMemory:
         return MULTIBOOT_MEMORY_ACPI_RECLAIMABLE;

      case EfiACPIMemoryNVS:
         return MULTIBOOT_MEMORY_NVS;

      case EfiMemoryMappedIO:
      case EfiMemoryMappedIOPortSpace:
      case EfiPalCode:
         return MULTIBOOT_MEMORY_RESERVED;
   }
}

void AddMemoryRegion(UINT64 start, UINT64 end, UINT32 type)
{
   if (type == MULTIBOOT_MEMORY_AVAILABLE) {
      if (start < mbi->mem_lower * KB)
         mbi->mem_lower = start / KB;

      if (end > mbi->mem_upper * KB)
         mbi->mem_upper = end / KB;
   }

   multiboot_mmap[mmap_elems_count++] = (multiboot_memory_map_t) {
      .size = sizeof(multiboot_memory_map_t) - sizeof(u32),
      .addr = (multiboot_uint64_t)start,
      .len = (multiboot_uint64_t)(end - start),
      .type = type
   };
}

EFI_STATUS
MultibootSaveMemoryMap(UINTN *mapkey)
{
   EFI_STATUS status = EFI_SUCCESS;
   UINT32 last_type = (UINT32) -1;
   UINT64 last_start = 0;
   UINT64 last_end = 0;
   UINTN mmap_size;
   UINTN desc_size;
   UINT32 desc_ver;

   mbi->flags |= MULTIBOOT_INFO_MEM_MAP;

   multiboot_mmap =
      (void *)(UINTN)mbi->mods_addr +
         (mbi->mods_count * sizeof(multiboot_module_t));

   mmap_size = sizeof(mmap);
   status = BS->GetMemoryMap(&mmap_size, mmap, mapkey, &desc_size, &desc_ver);
   HANDLE_EFI_ERROR("BS->GetMemoryMap");

   EFI_MEMORY_DESCRIPTOR *desc = (void *)mmap;

   do {

      UINT32 type = efi_to_multiboot_mem_type(desc->Type);
      UINT64 start = desc->PhysicalStart;
      UINT64 end = start + desc->NumberOfPages * 4096;

      if (last_type != type || last_end != start) {

         /*
          * The new region is not contiguous with the previous one OR it has
          * a different type.
          */

         if (last_type != (UINT32)-1) {
            AddMemoryRegion(last_start, last_end, last_type);
         }

         last_type = type;
         last_start = start;
      }

      /*
       * last_type == type && last_end == start
       *
       * We're continuing a region of the same "multiboot type", just move the
       * end forward.
       */
      last_end = end;

      desc = (void *)desc + desc_size;

   } while ((UINTN)desc < (UINTN)mmap + mmap_size);

   AddMemoryRegion(last_start, last_end, last_type);

   mbi->mmap_addr = (UINTN)multiboot_mmap;
   mbi->mmap_length = mmap_elems_count * sizeof(multiboot_memory_map_t);

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
   UINTN ramdisk_size;

   UINTN bufSize;
   void *kernel_entry = NULL;
   multiboot_module_t *mod;

   UINTN xres, yres;

   InitializeLib(image, ST);

   Print(L"----- Hello from exOS's UEFI bootloader! -----\r\n\r\n");

   status = SetupGraphicMode(BS, &xres, &yres);
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

   EFI_PHYSICAL_ADDRESS multiboot_buffer = 64 * KB;

   status = BS->AllocatePages(AllocateMaxAddress,
                              EfiLoaderData,
                              4,
                              &multiboot_buffer);
   HANDLE_EFI_ERROR("AllocatePages");

   BS->SetMem((void *)(UINTN)multiboot_buffer, PAGE_SIZE, 0);

   mbi = (multiboot_info_t *)(UINTN)multiboot_buffer;
   mod = (multiboot_module_t *)(UINTN)(multiboot_buffer + sizeof(*mbi));

   SetMbiFramebufferInfo(mbi, xres, yres);

   mbi->mem_lower = 0;
   mbi->mem_upper = 0;

   mbi->flags |= MULTIBOOT_INFO_MODS;
   mbi->mods_addr = (UINTN)mod;
   mbi->mods_count = 1;
   mod->mod_start = ramdisk_paddr;
   mod->mod_end = mod->mod_start + ramdisk_size;

   // Prepare for the actual boot
   // Print(L"mbi buffer: 0x%x\n", multiboot_buffer);
   // Print(L"RAMDISK paddr: 0x%x\n", ramdisk_paddr);
   // Print(L"RAMDISK size: %u\n", ramdisk_size);

   // Print(L"Press ANY key to boot the kernel...\r\n");
   // WaitForKeyPress(ST);

   UINTN mapkey;
   status = MultibootSaveMemoryMap(&mapkey);
   HANDLE_EFI_ERROR("MultibootSaveMemoryMap() failed");

   status = BS->ExitBootServices(image, mapkey);
   HANDLE_EFI_ERROR("BS->ExitBootServices");

   jump_to_kernel(mbi, kernel_entry);

end:
   /* --- we should never get here in the normal case --- */
   WaitForKeyPress(ST);
   return EFI_SUCCESS;
}


