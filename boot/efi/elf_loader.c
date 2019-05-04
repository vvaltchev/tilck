/* SPDX-License-Identifier: BSD-2-Clause */

#include "defs.h"
#include "utils.h"
#include <elf.h>

#define TEMP_KERNEL_ADDR   (KERNEL_PADDR + KERNEL_MAX_SIZE)
#define KERNEL_FILE        CONCAT(L, KERNEL_FILE_PATH_EFI)

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
                   phdr->p_vaddr + phdr->p_filesz))
      {
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
