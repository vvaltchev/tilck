/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include "defs.h"

#define PAGE_SIZE         4096

#define CHECK(cond)                                  \
   do {                                              \
      if (!(cond)) {                                 \
         Print(L"CHECK '%a' FAILED\r\n", #cond);     \
         status = EFI_LOAD_ERROR;                    \
         goto end;                                   \
      }                                              \
   } while(0)

#define HANDLE_EFI_ERROR(op)                                 \
    do {                                                     \
       if (EFI_ERROR(status)) {                              \
          Print(L"[%a] Error: %r\r\n", op, status);          \
          goto end;                                          \
       }                                                     \
    } while (0)

extern EFI_MEMORY_DESCRIPTOR mmap[512];
extern UINTN mmap_size;
extern UINTN desc_size;

EFI_STATUS GetMemoryMap(UINTN *mapkey);
EFI_MEMORY_DESCRIPTOR *GetMemDescForAddress(EFI_PHYSICAL_ADDRESS paddr);
EFI_STATUS KernelLoadMemoryChecks(void);

EFI_INPUT_KEY WaitForKeyPress(EFI_SYSTEM_TABLE *ST);

EFI_STATUS
LoadFileFromDisk(EFI_BOOT_SERVICES *BS,
                 EFI_FILE_PROTOCOL *fileProt,
                 INTN pagesCount,
                 EFI_PHYSICAL_ADDRESS *paddr,
                 CHAR16 *filePath);

void JumpToKernel(multiboot_info_t *mbi, void *entry_point);

