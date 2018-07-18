
#include <exos/common/basic_defs.h>
#include <exos/common/failsafe_assert.h>
#include <exos/common/fat32_base.h>

/* We HAVE to undef our ASSERT because the gnu-efi headers define it */
#undef ASSERT

#include <efi.h>
#include <efilib.h>
#include <multiboot.h>
#include "efierr.h"

#include "utils.h"

EFI_STATUS
LoadRamdisk(EFI_HANDLE image,
            EFI_LOADED_IMAGE *loaded_image,
            EFI_PHYSICAL_ADDRESS *ramdisk_paddr_ref,
            UINTN *ramdisk_size)
{
   EFI_STATUS status = EFI_SUCCESS;
   EFI_BLOCK_IO_PROTOCOL *blockio;
   EFI_DISK_IO_PROTOCOL *ioprot;

   u32 sector_size;
   u32 sectors_per_fat;
   u32 total_fat_size;
   u32 total_used_bytes;
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

   Print(L"Loading ramdisk...\r\n");

   status = BS->AllocatePages(AllocateAnyPages,
                              EfiLoaderData,
                              1, /* just 1 page */
                              ramdisk_paddr_ref);
   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = (void *)(UINTN)*ramdisk_paddr_ref;

   status = ioprot->ReadDisk(ioprot,
                             blockio->Media->MediaId,
                             0, // offset from the beginnig of the partition!
                             1 * KB, /* just the header */
                             fat_hdr);
   HANDLE_EFI_ERROR("ReadDisk");


   sector_size = fat_get_sector_size(fat_hdr);
   sectors_per_fat = fat_get_FATSz(fat_hdr);
   total_fat_size = (fat_get_first_data_sector(fat_hdr) + 1) * sector_size;

   status = BS->FreePages(*ramdisk_paddr_ref, 1);
   HANDLE_EFI_ERROR("FreePages");

   /* Now allocate memory for storing the whole FAT table */

   status = BS->AllocatePages(AllocateAnyPages,
                              EfiLoaderData,
                              (total_fat_size / PAGE_SIZE) + 1,
                              ramdisk_paddr_ref);
   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = (void *)(UINTN)*ramdisk_paddr_ref;

   status = ioprot->ReadDisk(ioprot,
                             blockio->Media->MediaId,
                             0,
                             total_fat_size, /* only the FAT table */
                             fat_hdr);
   HANDLE_EFI_ERROR("ReadDisk");

   total_used_bytes = fat_get_used_bytes(fat_hdr);
   Print(L"RAMDISK used bytes: %u\n", total_used_bytes);

   //*ramdisk_size = fat_get_TotSec(fat_hdr) * sector_size;

   /*
    * Pass via multiboot 'used bytes' as RAMDISK size instead of the real
    * RAMDISK size. This is useful if the kernel uses the RAMDISK read-only.
    */
   *ramdisk_size = total_used_bytes;


   /*
    * Now we know everything. Free the memory used so far and allocate the
    * big buffer to store all the "used" clusters of the FAT32 partition,
    * including clearly the header and the FAT table.
    */

   status = BS->FreePages(*ramdisk_paddr_ref, (total_fat_size / PAGE_SIZE) + 1);
   HANDLE_EFI_ERROR("FreePages");

   *ramdisk_paddr_ref = KERNEL_PADDR + KERNEL_MAX_SIZE;

   status = BS->AllocatePages(AllocateAddress,
                              EfiLoaderData,
                              (total_used_bytes / PAGE_SIZE) + 1,
                              ramdisk_paddr_ref);
   HANDLE_EFI_ERROR("AllocatePages");
   fat_hdr = (void *)(UINTN)*ramdisk_paddr_ref;

   status = ioprot->ReadDisk(ioprot,
                             blockio->Media->MediaId,
                             0,
                             total_used_bytes,
                             fat_hdr);
   HANDLE_EFI_ERROR("ReadDisk");

end:
   return status;
}
