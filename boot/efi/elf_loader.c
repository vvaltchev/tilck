/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck_gen_headers/krn_max_sz.h>

#include "defs.h"
#include "utils.h"
#include <elf.h>

static inline bool
IsMemRegionUsable(EFI_MEMORY_DESCRIPTOR *m)
{
   return m->Type == EfiConventionalMemory ||
          m->Type == EfiBootServicesCode   ||
          m->Type == EfiBootServicesData;
}

static inline EFI_PHYSICAL_ADDRESS
GetEndOfRegion(EFI_MEMORY_DESCRIPTOR *m)
{
   return m->PhysicalStart + m->NumberOfPages * PAGE_SIZE;
}

EFI_STATUS
KernelLoadMemoryChecks(void)
{
   EFI_MEMORY_DESCRIPTOR *m;
   EFI_PHYSICAL_ADDRESS p = KERNEL_PADDR;
   EFI_PHYSICAL_ADDRESS pend = KERNEL_PADDR + KERNEL_MAX_SIZE;

   while (p < pend) {

      m = GetMemDescForAddress(p);

      if (!m) {
         Print(L"ERROR: unable to find memory region for kernel's paddr: "
               "0x%08x\n", p);
         return EFI_LOAD_ERROR;
      }

      if (!IsMemRegionUsable(m)) {

         Print(L"ERROR: kernel's load area contains unusable mem areas\n");
         Print(L"Kernel's load area:  0x%08x - 0x%08x\n", KERNEL_PADDR, pend);
         Print(L"Unusable mem region: 0x%08x - 0x%08x\n",
               m->PhysicalStart, GetEndOfRegion(m));
         Print(L"Region type: %d\n", m->Type);

         return EFI_LOAD_ERROR;
      }

      p = GetEndOfRegion(m);
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

   for (int i = 0; i < header->e_phnum; i++, phdr++) {

      // Ignore non-load segments.
      if (phdr->p_type != PT_LOAD)
         continue;

      CHECK(phdr->p_vaddr >= KERNEL_BASE_VA);
      CHECK(phdr->p_paddr >= KERNEL_PADDR);
      CHECK(phdr->p_paddr + phdr->p_memsz <= KERNEL_PADDR + KERNEL_MAX_SIZE);
   }

end:
   return status;
}

EFI_STATUS
LoadKernelFile(CHAR16 *filePath)
{
   EFI_STATUS status = EFI_LOAD_ERROR;

   if (kernel_file_paddr) {
      BS->FreePages(kernel_file_paddr, kernel_file_size);
   }

   /* Temporary load the whole kernel file in a safe location */
   status = LoadFileFromDisk(fileProt,
                             &kernel_file_paddr,
                             &kernel_file_size,
                             filePath);
   HANDLE_EFI_ERROR("LoadFileFromDisk");
   status = ElfChecks(kernel_file_paddr);

end:
   return status;
}
