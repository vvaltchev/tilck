
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

#define EFI_MBI_MAX_ADDR (64 * KB)
#define TEMP_KERNEL_ADDR  (KERNEL_PADDR + KERNEL_MAX_SIZE)

#define KERNEL_FILE CONCAT(L, KERNEL_FILE_PATH_EFI)

EFI_STATUS
LoadRamdisk(EFI_HANDLE image,
            EFI_LOADED_IMAGE *loaded_image,
            EFI_PHYSICAL_ADDRESS *ramdisk_paddr_ref,
            UINTN *ramdisk_size);

EFI_STATUS SetupGraphicMode(EFI_BOOT_SERVICES *BS, UINTN *xres, UINTN *yres);
void SetMbiFramebufferInfo(multiboot_info_t *mbi, u32 xres, u32 yres);

EFI_MEMORY_DESCRIPTOR mmap[512];

multiboot_info_t *mbi;
multiboot_memory_map_t *multiboot_mmap;
UINT32 mmap_elems_count = 0;

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


int EfiToMultibootMemType(UINT32 type)
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
   EFI_MEMORY_DESCRIPTOR *desc = NULL;
   EFI_STATUS status = EFI_SUCCESS;
   UINT32 last_type = (UINT32) -1;
   UINT64 last_start = 0;
   UINT64 last_end = 0;
   UINTN mmap_size;
   UINTN desc_size;
   UINT32 desc_ver;

   EFI_PHYSICAL_ADDRESS multiboot_mmap_paddr = EFI_MBI_MAX_ADDR;

   status = BS->AllocatePages(AllocateMaxAddress,
                              EfiLoaderData,
                              1,
                              &multiboot_mmap_paddr);
   HANDLE_EFI_ERROR("AllocatePages");

   BS->SetMem((void *)(UINTN)multiboot_mmap_paddr, 1 * PAGE_SIZE, 0);
   multiboot_mmap = (multiboot_memory_map_t *)(UINTN)multiboot_mmap_paddr;

   mmap_size = sizeof(mmap);
   status = BS->GetMemoryMap(&mmap_size, mmap, mapkey, &desc_size, &desc_ver);
   HANDLE_EFI_ERROR("BS->GetMemoryMap");

   mbi->flags |= MULTIBOOT_INFO_MEM_MAP;
   desc = (void *)mmap;

   do {

      UINT32 type = EfiToMultibootMemType(desc->Type);
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

EFI_STATUS
MbiSetRamdisk(EFI_PHYSICAL_ADDRESS ramdisk_paddr, UINTN ramdisk_size)
{
   EFI_STATUS status = EFI_SUCCESS;
   EFI_PHYSICAL_ADDRESS multiboot_mod_addr = EFI_MBI_MAX_ADDR;
   multiboot_module_t *mod;

   status = BS->AllocatePages(AllocateMaxAddress,
                              EfiLoaderData,
                              1,
                              &multiboot_mod_addr);
   HANDLE_EFI_ERROR("AllocatePages");

   BS->SetMem((void *)(UINTN)multiboot_mod_addr, 1 * PAGE_SIZE, 0);

   mod = (multiboot_module_t *)(UINTN)multiboot_mod_addr;
   mod->mod_start = ramdisk_paddr;
   mod->mod_end = mod->mod_start + ramdisk_size;

   mbi->flags |= MULTIBOOT_INFO_MODS;
   mbi->mods_addr = (UINTN)mod;
   mbi->mods_count = 1;

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
   UINTN xres, yres;
   UINTN bufSize;

   void *kernel_entry = NULL;

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

   EFI_PHYSICAL_ADDRESS multiboot_buffer = EFI_MBI_MAX_ADDR;

   status = BS->AllocatePages(AllocateMaxAddress,
                              EfiLoaderData,
                              1,
                              &multiboot_buffer);
   HANDLE_EFI_ERROR("AllocatePages");

   BS->SetMem((void *)(UINTN)multiboot_buffer, 1 * PAGE_SIZE, 0);

   mbi = (multiboot_info_t *)(UINTN)multiboot_buffer;
   SetMbiFramebufferInfo(mbi, xres, yres);

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


