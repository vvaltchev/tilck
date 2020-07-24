/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/page_size.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/fat32_base.h>
#include <tilck/common/utils.h>

/* We HAVE to undef our ASSERT because the gnu-efi headers define it */
#undef ASSERT

#include <efi.h>
#include <efilib.h>
#include <multiboot.h>
#include <efierr.h>

#include "utils.h"

#define LOADING_RAMDISK_STR            L"Loading ramdisk... "

static EFI_STATUS
LoadRamdisk_GetTotFatSize(EFI_BLOCK_IO_PROTOCOL *blockio,
                          UINT32 *total_fat_size_ref)
{
   const UINTN initrd_off = INITRD_SECTOR * SECTOR_SIZE;
   EFI_PHYSICAL_ADDRESS ramdisk_paddr = 0;
   EFI_STATUS status;
   UINT32 total_fat_size;
   UINT32 sector_size;
   void *fat_hdr;

   status = BS->AllocatePages(AllocateAnyPages,
                              EfiLoaderData,
                              1, /* just 1 page */
                              &ramdisk_paddr);
   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = TO_PTR(ramdisk_paddr);

   status = ReadAlignedBlock(blockio, initrd_off, PAGE_SIZE, fat_hdr);
   HANDLE_EFI_ERROR("ReadAlignedBlock");

   sector_size = fat_get_sector_size(fat_hdr);
   total_fat_size = (fat_get_first_data_sector(fat_hdr) + 1) * sector_size;

   status = BS->FreePages(ramdisk_paddr, 1);
   HANDLE_EFI_ERROR("FreePages");

   *total_fat_size_ref = total_fat_size;

end:
   return status;
}

static EFI_STATUS
LoadRamdisk_GetTotUsedBytes(EFI_BLOCK_IO_PROTOCOL *blockio,
                            UINT32 total_fat_size,
                            UINT32 *total_used_bytes_ref)
{
   const UINTN initrd_off = INITRD_SECTOR * SECTOR_SIZE;
   EFI_PHYSICAL_ADDRESS ramdisk_paddr = 0;
   UINT32 total_used_bytes;
   EFI_STATUS status;
   void *fat_hdr;

   /* Allocate memory for storing the whole FAT table */
   status = BS->AllocatePages(AllocateAnyPages,
                              EfiLoaderData,
                              (total_fat_size / PAGE_SIZE) + 1,
                              &ramdisk_paddr);
   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = TO_PTR(ramdisk_paddr);

   status = ReadAlignedBlock(blockio, initrd_off, total_fat_size, fat_hdr);
   HANDLE_EFI_ERROR("ReadAlignedBlock");

   total_used_bytes = fat_calculate_used_bytes(fat_hdr);

   /*
    * Now we know everything. Free the memory used so far and allocate the
    * big buffer to store all the "used" clusters of the FAT32 partition,
    * including clearly the header and the FAT table.
    */

   status = BS->FreePages(ramdisk_paddr, (total_fat_size / PAGE_SIZE) + 1);
   HANDLE_EFI_ERROR("FreePages");

   *total_used_bytes_ref = total_used_bytes;

end:
   return status;
}

EFI_STATUS
LoadRamdisk(EFI_SYSTEM_TABLE *ST,
            EFI_HANDLE image,
            EFI_LOADED_IMAGE *loaded_image,
            EFI_PHYSICAL_ADDRESS *ramdisk_paddr_ref,
            UINTN *ramdisk_size,
            UINTN CurrConsoleRow)
{
   const UINTN initrd_off = INITRD_SECTOR * SECTOR_SIZE;
   EFI_STATUS status = EFI_SUCCESS;
   EFI_BLOCK_IO_PROTOCOL *blockio = NULL;
   EFI_DEVICE_PATH *parentDpCopy = NULL;
   EFI_HANDLE parentDpHandle = NULL;


   u32 total_fat_size;
   u32 total_used_bytes;
   u32 ff_clu_off;
   void *fat_hdr;

   parentDpCopy = GetCopyOfParentDevicePathNode(
      DevicePathFromHandle(loaded_image->DeviceHandle)
   );

   status = GetHandlerForDevicePath(parentDpCopy,
                                    &BlockIoProtocol,
                                    &parentDpHandle);
   HANDLE_EFI_ERROR("GetHandlerForDevicePath");

   FreePool(parentDpCopy);
   parentDpCopy = NULL;

   status = BS->OpenProtocol(parentDpHandle,
                             &BlockIoProtocol,
                             (void **)&blockio,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("OpenProtocol(BlockIoProtocol)");

   Print(LOADING_RAMDISK_STR);

   status = LoadRamdisk_GetTotFatSize(blockio, &total_fat_size);
   HANDLE_EFI_ERROR("LoadRamdisk_GetTotFatSize");

   status = LoadRamdisk_GetTotUsedBytes(blockio,
                                        total_fat_size,
                                        &total_used_bytes);
   HANDLE_EFI_ERROR("LoadRamdisk_GetTotUsedBytes");

   /*
    * Because Tilck is 32-bit and it maps the first LINEAR_MAPPING_SIZE of
    * physical memory at KERNEL_BASE_VA, we really cannot accept ANY address
    * in the 64-bit space, because from Tilck we won't be able to read from
    * there. The address of the ramdisk we actually be at most:
    *
    *    LINEAR_MAPPING_SIZE - "size of ramdisk"
    *
    * The AllocateMaxAddress allocation type is exactly what we need to use in
    * this case. As explained in the UEFI specification:
    *
    *    Allocation requests of Type AllocateMaxAddress allocate any available
    *    range of pages whose uppermost address is less than or equal to the
    *    address pointed to by Memory on input.
    *
    * Additional notes
    * --------------------------
    * Note the `(total_used_bytes / PAGE_SIZE) + 2` expression below.
    *
    *    +1 page is added in order to brutally round-up the value of
    *    `total_used_bytes`, when turning into pages.
    *
    *    Another +1 page is added to allow, evenutally, the Tilck kernel
    *    to align it's data clusters at page boundary.
    */
   *ramdisk_paddr_ref = LINEAR_MAPPING_SIZE;
   status = BS->AllocatePages(AllocateMaxAddress,
                              EfiLoaderData,
                              (total_used_bytes / PAGE_SIZE) + 2,
                              ramdisk_paddr_ref);

   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = TO_PTR(*ramdisk_paddr_ref);

   status = ReadDiskWithProgress(ST->ConOut,
                                 CurrConsoleRow,
                                 LOADING_RAMDISK_STR,
                                 blockio,
                                 initrd_off,
                                 round_up_at(total_used_bytes, PAGE_SIZE),
                                 fat_hdr);
   HANDLE_EFI_ERROR("ReadDiskWithProgress");

   /* Now we're done with the BlockIoProtocol, close it. */
   BS->CloseProtocol(parentDpHandle, &BlockIoProtocol, image, NULL);
   blockio = NULL;

   ST->ConOut->SetCursorPosition(ST->ConOut, 0, CurrConsoleRow);
   Print(LOADING_RAMDISK_STR);
   Print(L"[ OK ]\r\n");
   ff_clu_off = fat_get_first_free_cluster_off(fat_hdr);

   if (ff_clu_off < total_used_bytes) {

      Print(L"Compacting ramdisk... ");

      fat_compact_clusters(fat_hdr);
      ff_clu_off = fat_get_first_free_cluster_off(fat_hdr);
      total_used_bytes = fat_calculate_used_bytes(fat_hdr);

      if (total_used_bytes != ff_clu_off) {

         Print(L"fat_compact_clusters failed: %u != %u\r\n",
               total_used_bytes, ff_clu_off);

         status = EFI_ABORTED;
         goto end;
      }

      Print(L"[ OK ]\r\n");
   }

   /*
    * Increase total_used_bytes by 1 page in order to allow Tilck's kernel to
    * align the first data sector, if necessary.
    */

   total_used_bytes += PAGE_SIZE;

   /*
    * Pass via multiboot 'used bytes' as RAMDISK size instead of the real
    * RAMDISK size. This is useful if the kernel uses the RAMDISK read-only.
    */
   *ramdisk_size = total_used_bytes;

end:

   if (parentDpCopy)
      FreePool(parentDpCopy);

   if (blockio)
      BS->CloseProtocol(parentDpHandle, &BlockIoProtocol, image, NULL);

   return status;
}
