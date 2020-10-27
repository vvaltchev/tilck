/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>

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
   EFI_PHYSICAL_ADDRESS pend = KERNEL_PADDR + get_loaded_kernel_mem_sz();

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
LoadKernelFile(CHAR16 *filePath, EFI_PHYSICAL_ADDRESS *paddr)
{
   static EFI_PHYSICAL_ADDRESS sPaddr;
   static UINTN sSize;

   EFI_STATUS status = EFI_LOAD_ERROR;

   if (sPaddr) {
      BS->FreePages(sPaddr, sSize / PAGE_SIZE);
   }

   /* Temporary load the whole kernel file in a safe location */
   status = LoadFileFromDisk(gFileProt,
                             &sPaddr,
                             &sSize,
                             filePath);
   HANDLE_EFI_ERROR("LoadFileFromDisk");
   *paddr = sPaddr;

end:
   return status;
}
