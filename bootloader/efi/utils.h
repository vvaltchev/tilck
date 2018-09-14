/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include "defs.h"

#define PAGE_SIZE         4096

#define _CONCAT(a, b) a##b
#define CONCAT(a, b) _CONCAT(a, b)


/*
 * Checks if 'addr' is in the range [begin, end).
 */
#define IN_RANGE(addr, begin, end) ((begin) <= (addr) && (addr) < (end))

#define CHECK(cond)                                  \
   do {                                              \
      if (!(cond)) {                                 \
         Print(L"CHECK '%a' FAILED\n", #cond);       \
         status = EFI_LOAD_ERROR;                    \
         goto end;                                   \
      }                                              \
   } while(0)

#define HANDLE_EFI_ERROR(op)                                 \
    do {                                                     \
       if (EFI_ERROR(status)) {                              \
          Print(L"[%a] Error: %r ", op, status);             \
          goto end;                                          \
       }                                                     \
    } while (0)

EFI_INPUT_KEY WaitForKeyPress(EFI_SYSTEM_TABLE *ST);

EFI_STATUS
LoadFileFromDisk(EFI_BOOT_SERVICES *BS,
                 EFI_FILE_PROTOCOL *fileProt,
                 INTN pagesCount,
                 EFI_PHYSICAL_ADDRESS paddr,
                 CHAR16 *filePath);

void JumpToKernel(multiboot_info_t *mbi, void *entry_point);


