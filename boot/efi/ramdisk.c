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
ReadAlignedBlock(EFI_BLOCK_IO_PROTOCOL *blockio,
                 UINTN offset,  /* offset in bytes, aligned to blockSize */
                 UINTN len,     /* length in bytes, aligned to blockSize */
                 void *buf)
{
   const UINT32 blockSize = blockio->Media->BlockSize;
   const UINT32 mediaId = blockio->Media->MediaId;
   EFI_STATUS status = EFI_SUCCESS;

   if (offset % blockSize) {
      status = EFI_INVALID_PARAMETER;
      goto end;
   }

   if (len < blockSize || (len % blockSize)) {
      status = EFI_BAD_BUFFER_SIZE;
      goto end;
   }

   status = blockio->ReadBlocks(blockio,
                                mediaId,
                                offset / blockSize,   /* offset in blocks */
                                len,                  /* length in bytes  */
                                buf);

   if (EFI_ERROR(status)) {

      Print(L"offset: %u\r\n", offset);
      Print(L"length: %u\r\n", len);
      Print(L"logical part: %u\r\n", blockio->Media->LogicalPartition);
      HANDLE_EFI_ERROR("ReadBlocks");
   }

end:
   return status;
}

static EFI_STATUS
ReadDiskWithProgress(SIMPLE_TEXT_OUTPUT_INTERFACE *ConOut,
                     UINTN CurrRow,
                     EFI_BLOCK_IO_PROTOCOL *blockio,
                     UINT64 Offset,
                     UINTN BufferSize,
                     void *Buffer)
{
   const UINTN ChunkSize = 256 * KB;
   const UINTN ChunkCount = BufferSize / ChunkSize;
   const UINTN rem = BufferSize - ChunkCount * ChunkSize;
   EFI_STATUS status;

   for (u32 chunk = 0; chunk < ChunkCount; chunk++) {

      if (chunk > 0) {
         ShowProgress(ST->ConOut,
                      CurrRow,
                      LOADING_RAMDISK_STR,
                      chunk * ChunkSize,
                      BufferSize);
      }

      status = ReadAlignedBlock(blockio, Offset, ChunkSize, Buffer);
      HANDLE_EFI_ERROR("ReadAlignedBlock");

      Offset += ChunkSize;
      Buffer += ChunkSize;
   }

   if (rem > 0) {
      status = ReadAlignedBlock(blockio, Offset, rem, Buffer);
      HANDLE_EFI_ERROR("ReadAlignedBlock");
   }

   ShowProgress(ST->ConOut,
                CurrRow,
                LOADING_RAMDISK_STR,
                BufferSize,
                BufferSize);

end:
   return status;
}


static EFI_DEVICE_PATH *
DevicePathGetLastValidNode(EFI_DEVICE_PATH *dp)
{
   EFI_DEVICE_PATH *curr = dp;
   EFI_DEVICE_PATH *prev = dp;

   for (; !IsDevicePathEnd(curr); curr = NextDevicePathNode(curr)) {
      prev = curr;
   }

   return prev;
}

static void
TruncateDevicePath(EFI_DEVICE_PATH *dp)
{
   EFI_DEVICE_PATH *lastDp = DevicePathGetLastValidNode(dp);
   SetDevicePathEndNode(lastDp);
}

static EFI_DEVICE_PATH *
GetCopyOfParentDevicePathNode(EFI_DEVICE_PATH *dp)
{
   EFI_DEVICE_PATH *parent = DuplicateDevicePath(dp);
   TruncateDevicePath(parent);
   return parent;
}

static EFI_STATUS
GetHandlerForDevicePath(EFI_DEVICE_PATH *dp,
                        EFI_GUID *supportedProt,
                        EFI_HANDLE *refHandle)
{
   EFI_DEVICE_PATH *dpCopy = dp;
   EFI_STATUS status = EFI_SUCCESS;

   status = BS->LocateDevicePath(supportedProt, &dpCopy, refHandle);
   HANDLE_EFI_ERROR("LocateDevicePath");

   if (!IsDevicePathEnd(dpCopy)) {
      Print(L"ERROR: Cannot get a handler for device path:\r\n");
      Print(L"    \"%s\"\r\n", DevicePathToStr(dp));
      Print(L"ERROR: Closest match:\r\n");
      Print(L"    \"%s\"\r\n", DevicePathToStr(dpCopy));
      status = EFI_ABORTED;
   }

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

   u32 sector_size;
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
   *ramdisk_paddr_ref = 0;
   status = BS->AllocatePages(AllocateAnyPages,
                              EfiLoaderData,
                              1, /* just 1 page */
                              ramdisk_paddr_ref);
   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = TO_PTR(*ramdisk_paddr_ref);

   status = ReadAlignedBlock(blockio, initrd_off, PAGE_SIZE, fat_hdr);
   HANDLE_EFI_ERROR("ReadAlignedBlock");

   sector_size = fat_get_sector_size(fat_hdr);
   total_fat_size = (fat_get_first_data_sector(fat_hdr) + 1) * sector_size;

   status = BS->FreePages(*ramdisk_paddr_ref, 1);
   HANDLE_EFI_ERROR("FreePages");

   /* Now allocate memory for storing the whole FAT table */

   status = BS->AllocatePages(AllocateAnyPages,
                              EfiLoaderData,
                              (total_fat_size / PAGE_SIZE) + 1,
                              ramdisk_paddr_ref);
   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = TO_PTR(*ramdisk_paddr_ref);

   status = ReadAlignedBlock(blockio, initrd_off, total_fat_size, fat_hdr);
   HANDLE_EFI_ERROR("ReadAlignedBlock");

   total_used_bytes = fat_calculate_used_bytes(fat_hdr);

   /*
    * Now we know everything. Free the memory used so far and allocate the
    * big buffer to store all the "used" clusters of the FAT32 partition,
    * including clearly the header and the FAT table.
    */

   status = BS->FreePages(*ramdisk_paddr_ref, (total_fat_size / PAGE_SIZE) + 1);
   HANDLE_EFI_ERROR("FreePages");

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
