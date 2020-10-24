/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/page_size.h>
#include <tilck/boot/common.h>

#include <efi.h>
#include <efilib.h>
#include <multiboot.h>

extern multiboot_info_t *mbi;
extern EFI_HANDLE gImageHandle;
extern EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt;
extern const struct bootloader_intf efi_boot_intf;

EFI_STATUS
LoadKernelFile(EFI_FILE_PROTOCOL *fileProt,
               EFI_PHYSICAL_ADDRESS *filePaddr);

EFI_STATUS AllocateMbi(void);
EFI_STATUS MultibootSaveMemoryMap(UINTN *mapkey);
EFI_STATUS MbiSetBootloaderName(void);
EFI_STATUS MbiSetPointerToAcpiTable(void);
EFI_STATUS MbiSetRamdisk(EFI_PHYSICAL_ADDRESS ramdisk_paddr,
                         UINTN ramdisk_size);


void
MbiSetFramebufferInfo(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info,
                      UINTN fb_addr);

EFI_STATUS
LoadRamdisk(EFI_HANDLE image,
            EFI_LOADED_IMAGE *loaded_image,
            EFI_PHYSICAL_ADDRESS *ramdisk_paddr_ref,
            UINTN *ramdisk_size,
            UINTN CurrentConsoleRow); /* HACK: see ShowProgress() */

void
AskUserToChooseVideoMode(void);

bool
SwitchToUserSelectedMode(void);

EFI_STATUS
EarlySetDefaultResolution(void);
