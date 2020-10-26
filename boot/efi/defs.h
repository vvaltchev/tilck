/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/page_size.h>
#include <tilck/boot/common.h>

#include <efi.h>
#include <efilib.h>
#include <multiboot.h>

#ifndef __EFI_MAIN__
   #define EXTERN extern
#else
   #define EXTERN
#endif

EXTERN multiboot_info_t *gMbi;
EXTERN EFI_HANDLE gImageHandle;
EXTERN EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt;
EXTERN EFI_FILE_PROTOCOL *gFileProt;
EXTERN EFI_PHYSICAL_ADDRESS gRamdiskPaddr;
EXTERN UINTN gRamdiskSize;

extern const struct bootloader_intf efi_boot_intf;

EFI_STATUS LoadKernelFile(CHAR16 *filePath, EFI_PHYSICAL_ADDRESS *paddr);
EFI_STATUS AllocateMbi(void);
EFI_STATUS MultibootSaveMemoryMap(UINTN *mapkey);
EFI_STATUS MbiSetBootloaderName(void);
EFI_STATUS MbiSetPointerToAcpiTable(void);
EFI_STATUS MbiSetRamdisk(void);


void
MbiSetFramebufferInfo(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info,
                      UINTN fb_addr);

EFI_STATUS
ReserveMemAreaForKernelImage(void);

EFI_STATUS
LoadRamdisk(EFI_HANDLE image,
            EFI_LOADED_IMAGE *loaded_image,
            EFI_PHYSICAL_ADDRESS *ramdisk_paddr_ref,
            UINTN *gRamdiskSize,
            UINTN CurrentConsoleRow); /* HACK: see ShowProgress() */

EFI_STATUS
EarlySetDefaultResolution(void);
