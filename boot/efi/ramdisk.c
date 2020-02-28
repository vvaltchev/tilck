/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/fat32_base.h>

/* We HAVE to undef our ASSERT because the gnu-efi headers define it */
#undef ASSERT

#include <efi.h>
#include <efilib.h>
#include <multiboot.h>
#include <efierr.h>

#include "utils.h"

#define LOADING_RAMDISK_STR            L"Loading ramdisk... "

static EFI_STATUS
ReadDiskWithProgress(SIMPLE_TEXT_OUTPUT_INTERFACE *ConOut,
                     UINTN CurrRow,
                     EFI_DISK_IO_PROTOCOL *prot,
                     UINT32 MediaId,
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

      status = prot->ReadDisk(prot,
                              MediaId,
                              Offset,
                              ChunkSize,
                              Buffer);
      HANDLE_EFI_ERROR("ReadDisk");

      Offset += ChunkSize;
      Buffer += ChunkSize;
   }

   if (rem > 0) {
      status = prot->ReadDisk(prot,
                              MediaId,
                              Offset,
                              rem,
                              Buffer);
      HANDLE_EFI_ERROR("ReadDisk");
   }

   ShowProgress(ST->ConOut,
                CurrRow,
                LOADING_RAMDISK_STR,
                BufferSize,
                BufferSize);

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
   EFI_STATUS status = EFI_SUCCESS;
   EFI_BLOCK_IO_PROTOCOL *blockio;
   EFI_DISK_IO_PROTOCOL *ioprot;

   u32 sector_size;
   u32 total_fat_size;
   u32 total_used_bytes;
   u32 ff_clu_off;
   void *fat_hdr;

   status = BS->OpenProtocol(loaded_image->DeviceHandle,
                             &BlockIoProtocol,
                             (void **)&blockio,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("Getting a BlockIoProtocol handle");

   status = BS->OpenProtocol(loaded_image->DeviceHandle,
                             &DiskIoProtocol,
                             (void **)&ioprot,
                             image,
                             NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("Getting a DiskIOProtocol handle");

   Print(LOADING_RAMDISK_STR);

   *ramdisk_paddr_ref = 0;
   status = BS->AllocatePages(AllocateAnyPages,
                              EfiLoaderData,
                              1, /* just 1 page */
                              ramdisk_paddr_ref);
   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = TO_PTR(*ramdisk_paddr_ref);

   status = ioprot->ReadDisk(ioprot,
                             blockio->Media->MediaId,
                             0, /* offset from the beginnig of the partition! */
                             1 * KB, /* just the header */
                             fat_hdr);
   HANDLE_EFI_ERROR("ReadDisk");

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

   status = ioprot->ReadDisk(ioprot,
                             blockio->Media->MediaId,
                             0,
                             total_fat_size, /* only the FAT table */
                             fat_hdr);
   HANDLE_EFI_ERROR("ReadDisk");

   total_used_bytes = fat_calculate_used_bytes(fat_hdr);

   /*
    * Now we know everything. Free the memory used so far and allocate the
    * big buffer to store all the "used" clusters of the FAT32 partition,
    * including clearly the header and the FAT table.
    */

   status = BS->FreePages(*ramdisk_paddr_ref, (total_fat_size / PAGE_SIZE) + 1);
   HANDLE_EFI_ERROR("FreePages");

   status = BS->AllocatePages(AllocateAnyPages,
                              EfiLoaderData,
                              (total_used_bytes / PAGE_SIZE) + 1,
                              ramdisk_paddr_ref);

   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = TO_PTR(*ramdisk_paddr_ref);

   status = ReadDiskWithProgress(ST->ConOut,
                                 CurrConsoleRow,
                                 ioprot,
                                 blockio->Media->MediaId,
                                 0,
                                 total_used_bytes,
                                 fat_hdr);
   HANDLE_EFI_ERROR("ReadDiskWithProgress");

   ST->ConOut->SetCursorPosition(ST->ConOut, 0, 2);
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

   // Print(L"RAMDISK used bytes: %u\r\n", total_used_bytes);

   /*
    * Pass via multiboot 'used bytes' as RAMDISK size instead of the real
    * RAMDISK size. This is useful if the kernel uses the RAMDISK read-only.
    */
   *ramdisk_size = total_used_bytes;

end:
   return status;
}
