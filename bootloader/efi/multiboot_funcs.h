
#pragma once

#include <efi.h>
#include <efilib.h>
#include <multiboot.h>

extern multiboot_info_t *mbi;

EFI_STATUS AllocateMbi(void);
EFI_STATUS MultibootSaveMemoryMap(UINTN *mapkey);
EFI_STATUS MbiSetRamdisk(EFI_PHYSICAL_ADDRESS ramdisk_paddr,
                         UINTN ramdisk_size);

EFI_STATUS
MbiSetFramebufferInfo(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info,
                      UINTN fb_addr);
