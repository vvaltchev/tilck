/* SPDX-License-Identifier: BSD-2-Clause */

#include "defs.h"
#include "utils.h"
#include <elf.h>

#define KERNEL_FILE        CONCAT(L, KERNEL_FILE_PATH_EFI)

EFI_STATUS
KernelLoadMemoryChecks(void)
{
   EFI_MEMORY_DESCRIPTOR *mem_desc1;
   EFI_MEMORY_DESCRIPTOR *mem_desc2;

   mem_desc1 = GetMemDescForAddress(KERNEL_PADDR);

   if (!mem_desc1) {
      Print(L"ERROR: unable to find memory type for KERNEL's paddr\r\n");
      return EFI_LOAD_ERROR;
   }

   mem_desc2 = GetMemDescForAddress(KERNEL_PADDR + KERNEL_MAX_SIZE - 1);

   if (!mem_desc2) {
      Print(L"ERROR: unable to find memory type for KERNEL's end paddr\r\n");
      return EFI_LOAD_ERROR;
   }

   if (mem_desc1 != mem_desc2) {
      Print(L"ERROR: multiple regions in kernel's load paddr range\r\n");
      return EFI_LOAD_ERROR;
   }

   if (mem_desc1->Type != EfiConventionalMemory &&
       mem_desc1->Type != EfiBootServicesCode &&
       mem_desc1->Type != EfiBootServicesData)
   {
      Print(L"ERROR: kernel's load paddr range in unused memory region\r\n");
      return EFI_LOAD_ERROR;
   }

   return EFI_SUCCESS;
}

EFI_STATUS
ElfChecks(EFI_PHYSICAL_ADDRESS filePaddr)
{
   EFI_STATUS status = EFI_SUCCESS;
   Elf32_Ehdr *header = (Elf32_Ehdr *)(UINTN)filePaddr;

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
   }

end:
   return status;
}

EFI_STATUS
LoadKernelFile(EFI_BOOT_SERVICES *BS,
               EFI_FILE_PROTOCOL *fileProt,
               EFI_PHYSICAL_ADDRESS *filePaddr)
{
   EFI_STATUS status = EFI_LOAD_ERROR;

   /* Temporary load the whole kernel file in a safe location */
   status = LoadFileFromDisk(BS,
                             fileProt,
                             KERNEL_MAX_SIZE / PAGE_SIZE,
                             filePaddr,
                             KERNEL_FILE);

   HANDLE_EFI_ERROR("LoadFileFromDisk");
   Print(L"Kernel file loaded at temporary paddr: 0x%08x\n", *filePaddr);
   status = ElfChecks(*filePaddr);

end:
   return status;
}
