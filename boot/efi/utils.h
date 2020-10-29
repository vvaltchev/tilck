/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include "defs.h"

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
          Print(L"[%a] Error: %r\n", op, status);            \
          goto end;                                          \
       }                                                     \
    } while (0)

void
JumpToKernel(void *entry_point);

EFI_STATUS
GetMemoryMap(UINTN *mapkey);

EFI_MEMORY_DESCRIPTOR *
GetMemDescForAddress(EFI_PHYSICAL_ADDRESS paddr);

EFI_STATUS
KernelLoadMemoryChecks(void);

EFI_INPUT_KEY
WaitForKeyPress(void);

EFI_STATUS
LoadFileFromDisk(EFI_FILE_PROTOCOL *fileProt,
                 EFI_PHYSICAL_ADDRESS *paddr,
                 UINTN *fileSz,
                 CHAR16 *filePath);

void
ShowProgress(SIMPLE_TEXT_OUTPUT_INTERFACE *ConOut,
             const CHAR16 *PrefixStr,
             UINTN curr,
             UINTN tot);

EFI_STATUS
ReadDiskWithProgress(SIMPLE_TEXT_OUTPUT_INTERFACE *ConOut,
                     const CHAR16 *loadingStr,
                     EFI_BLOCK_IO_PROTOCOL *blockio,
                     UINT64 Offset,
                     UINTN BufferSize,
                     void *Buffer);

EFI_STATUS
ReadAlignedBlock(EFI_BLOCK_IO_PROTOCOL *blockio,
                 UINTN offset,  /* offset in bytes, aligned to blockSize */
                 UINTN len,     /* length in bytes, aligned to blockSize */
                 void *buf);

EFI_DEVICE_PATH *
DevicePathGetLastValidNode(EFI_DEVICE_PATH *dp);

void
TruncateDevicePath(EFI_DEVICE_PATH *dp);

EFI_DEVICE_PATH *
GetCopyOfParentDevicePathNode(EFI_DEVICE_PATH *dp);

EFI_STATUS
GetHandlerForDevicePath(EFI_DEVICE_PATH *dp,
                        EFI_GUID *supportedProt,
                        EFI_HANDLE *refHandle);

