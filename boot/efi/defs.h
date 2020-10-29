/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/page_size.h>
#include <tilck/boot/common.h>

#undef ASSERT
#include <efi.h>
#include <efilib.h>
#include <efierr.h>
#include <tilck/common/assert.h>

#include <multiboot.h>

#ifndef __EFI_MAIN__
   #define EXTERN extern
#else
   #define EXTERN
#endif

#define EFI_MBI_MAX_ADDR                                 (64 * KB)

EXTERN multiboot_info_t *gMbi;
EXTERN EFI_HANDLE gImageHandle;
EXTERN EFI_LOADED_IMAGE *gLoadedImage;
EXTERN EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt;
EXTERN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *gFileFsProt;
EXTERN EFI_FILE_PROTOCOL *gFileProt;
EXTERN EFI_PHYSICAL_ADDRESS gRamdiskPaddr;
EXTERN UINTN gRamdiskSize;
EXTERN EFI_MEMORY_DESCRIPTOR gMmap[512];
EXTERN UINTN gMmap_size;
EXTERN UINTN gDesc_size;
EXTERN char *gCmdlineBuf;
EXTERN bool gExitBootServicesCalled;

extern const struct bootloader_intf efi_boot_intf;

EFI_STATUS LoadKernelFile(CHAR16 *filePath, EFI_PHYSICAL_ADDRESS *paddr);
EFI_STATUS MultibootSaveMemoryMap(UINTN *mapkey);
EFI_STATUS SetupMultibootInfo(void);

EFI_STATUS
ReserveMemAreaForKernelImage(void);

EFI_STATUS
LoadRamdisk(EFI_HANDLE image,
            EFI_LOADED_IMAGE *loaded_image,
            EFI_PHYSICAL_ADDRESS *ramdisk_paddr_ref,
            UINTN *gRamdiskSize);

EFI_STATUS
EarlySetDefaultResolution(void);
