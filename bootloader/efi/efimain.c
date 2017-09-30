
#include <efi.h>
#include <efilib.h>

#include "efibind.h"
#include "efidef.h"
#include "efidevp.h"
#include "eficon.h"
#include "efiapi.h"
#include "efierr.h"
#include "efiprot.h"

#define PAGE_SIZE            0x1000    // 4 KB
#define BOOT_PADDR           0xC000
#define KERNEL_PADDR       0x100000    // +1 MB
#define RAMDISK_PADDR    0x08000000    // +128 MB
#define RAMDISK_SIZE    (35*1024*1024) // 35 MB, size of 'fatpart'
#define KERNEL_MAX_SIZE (500 * 1024)   // 500 KB, max size of kernel.bin

/*
 * The folloing offsets cannot be easily (maybe not at all?) used by this loader
 * as the offset parameter in ReadDisk() takes an offset from the beginning of
 * the FAT32 partition. Here, in my understanding 'Disk' stands for 'partition'.
 * Since kernel is actually located at sector 4, not inside any partition, it
 * is hardly accessible.
 */

// #define KERNEL_OFFSET        0x1000    // +4 KB
// #define RAMDISK_OFFSET     0x100000    // +1 MB


void WaitForKeyPress(EFI_SYSTEM_TABLE *ST)
{
    UINTN index, k;
    EFI_EVENT event = ST->ConIn->WaitForKey;
    uefi_call_wrapper(BS->WaitForEvent,
                      3, // args count
                      1, // number of events in the array pointed by &event
                      &event, // pointer to events array (1 elem in our case).
                      &index); // index of the last matching event in the array

    // Read the key, allowing WaitForKey to block again.
    uefi_call_wrapper(ST->ConIn->ReadKeyStroke,
                      2,
                      ST->ConIn,
                      &k);
}

#define HANDLE_EFI_ERROR(op)                                 \
    do {                                                     \
       if (EFI_ERROR(status)) {                              \
          Print(L"[%a] Error: %r ", op, status);             \
          goto end;                                          \
       }                                                     \
    } while (0)


EFI_STATUS
LoadFileFromDisk(EFI_BOOT_SERVICES *BS,
                 EFI_FILE_PROTOCOL *fileProt,
                 INTN pagesCount,
                 EFI_PHYSICAL_ADDRESS paddr,
                 CHAR16 *filePath)
{
   EFI_STATUS status = EFI_SUCCESS;
   EFI_FILE_PROTOCOL *fileHandle;
   UINTN bufSize = pagesCount * PAGE_SIZE;
   UINT32 crc32 = 0;

   Print(L"AllocatePages for '%s'..\r\n", filePath);

   status = uefi_call_wrapper(BS->AllocatePages,
                              4,
                              AllocateAddress,
                              EfiBootServicesData,
                              pagesCount,
                              &paddr);
   HANDLE_EFI_ERROR("AllocatePages");

   Print(L"File Open('%s')...\r\n", filePath);
   status = uefi_call_wrapper(fileProt->Open, 5, fileProt,
                              &fileHandle, filePath,
                              EFI_FILE_MODE_READ, 0);
   HANDLE_EFI_ERROR("fileProt->Open");

   Print(L"File Read()...\r\n");

   status = uefi_call_wrapper(fileProt->Read, 3,
                              fileHandle, &bufSize, (void *)paddr);
   HANDLE_EFI_ERROR("fileProt->Read");


   Print(L"Size read: %d\r\n", bufSize);

   uefi_call_wrapper(BS->CalculateCrc32, 3, paddr, bufSize, &crc32);
   Print(L"Crc32: 0x%x\r\n", crc32);

   status = uefi_call_wrapper(fileHandle->Close, 1, fileHandle);
   HANDLE_EFI_ERROR("fileHandle->Close");

end:
   return status;
}

void AlignedMemCpy(UINTN *src, UINTN *dst, UINTN count)
{
   for (UINTN i = count-1; i >= 0; i--) {
      dst[i] = src[i];
   }
}

void DumpFirst16Bytes(char *buf)
{
   Print(L"First 16 bytes in hex: \r\n");
   for (int i = 0; i < 16; i++) {
      Print(L"%02x ", (unsigned char)buf[i]);
   }
   Print(L"\r\n");
}

/**
 * efi_main - The entry point for the EFI application
 * @image: firmware-allocated handle that identifies the image
 * @ST: EFI system table
 */
EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *ST)
{
   EFI_STATUS status;
   EFI_LOADED_IMAGE *loaded_image;
   EFI_DEVICE_PATH *device_path;
   EFI_BLOCK_IO_PROTOCOL *blockio;
   EFI_DISK_IO_PROTOCOL *ioprot;
   EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fileFsProt;
   EFI_FILE_PROTOCOL *fileProt;
   EFI_FILE_PROTOCOL *fileHandle;
   UINTN bufSize;
   UINTN crc32;
   UINTN ramdisk_paddr = RAMDISK_PADDR;
   EFI_BOOT_SERVICES *BS = ST->BootServices;


   InitializeLib(image, ST);

   Print(L"--- exOS bootloader ---\r\n");

   status = uefi_call_wrapper(BS->OpenProtocol,
                              6,                                  // #args
                              image,                              // arg 1
                              &LoadedImageProtocol,               // arg 2
                              &loaded_image,                      // arg 3
                              image,                              // arg 4
                              NULL,                               // arg 5
                              EFI_OPEN_PROTOCOL_GET_PROTOCOL);    // arg 6

   HANDLE_EFI_ERROR("Getting a LoadedImageProtocol handle");

   status = uefi_call_wrapper(BS->OpenProtocol,
                              6,
                              loaded_image->DeviceHandle,
                              &BlockIoProtocol,
                              &blockio,
                              image,
                              NULL,
                              EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("Getting a BlockIoProtocol handle");

   status = uefi_call_wrapper(BS->OpenProtocol, 6,
                              loaded_image->DeviceHandle,
                              &DiskIoProtocol,
                              &ioprot,
                              image,
                              NULL,
                              EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("Getting a DiskIOProtocol handle");

   Print(L"Loading ramdisk...\r\n");

   status = uefi_call_wrapper(BS->AllocatePages,
                              4,
                              AllocateAnyPages,
                              EfiBootServicesData,
                              RAMDISK_SIZE / PAGE_SIZE,
                              &ramdisk_paddr);

   HANDLE_EFI_ERROR("AllocatePages");

   status = uefi_call_wrapper(ioprot->ReadDisk,
                              5, /* #args */
                              ioprot,
                              blockio->Media->MediaId,
                              0, // offset from the beginnig of the partition!
                              RAMDISK_SIZE /* buffer size */,
                              ramdisk_paddr);
   HANDLE_EFI_ERROR("ReadDisk");
   Print(L"Read OK\r\n");
   Print(L"RAMDISK paddr: 0x%lx\r\n", ramdisk_paddr);

   crc32 = 0;
   uefi_call_wrapper(BS->CalculateCrc32, 3,
                     ramdisk_paddr, RAMDISK_SIZE, &crc32);
   Print(L"RAMDISK CRC32: 0x%x\r\n", crc32);

   // ------------------------------------------------------------------ //

   Print(L"OpenProtocol (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL)...\r\n");
   status = uefi_call_wrapper(BS->OpenProtocol, 6,
                              loaded_image->DeviceHandle,
                              &FileSystemProtocol, &fileFsProt,
                              image, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
   HANDLE_EFI_ERROR("OpenProtocol FileSystemProtocol");

   Print(L"OpenVolume()...\r\n");
   status = uefi_call_wrapper(fileFsProt->OpenVolume, 2, fileFsProt, &fileProt);
   HANDLE_EFI_ERROR("OpenVolume");

   // Load kernel.bin as a file from the 'fatpart'.
   status = LoadFileFromDisk(BS, fileProt, KERNEL_MAX_SIZE / PAGE_SIZE,
                             KERNEL_PADDR, L"\\EFI\\BOOT\\kernel.bin");
   HANDLE_EFI_ERROR("LoadFileFromDisk");

   // Load switchmode.bin as a file from the 'fatpart'.

   status = LoadFileFromDisk(BS, fileProt, 1,
                             BOOT_PADDR, L"\\EFI\\BOOT\\switchmode.bin");
   HANDLE_EFI_ERROR("LoadFileFromDisk");


   // Prepare for the actual boot

   Print(L"Press ANY key to boot the kernel...\r\n");
   WaitForKeyPress(ST);

   EFI_MEMORY_DESCRIPTOR mmap[128];
   UINTN mmap_size, mapkey, desc_size, desc_ver;


   mmap_size = sizeof(mmap);

   status = uefi_call_wrapper(BS->GetMemoryMap, 5, &mmap_size,
                              mmap, &mapkey, &desc_size, &desc_ver);
   HANDLE_EFI_ERROR("BS->GetMemoryMap");

   status = uefi_call_wrapper(BS->ExitBootServices, 2, image, mapkey);
   HANDLE_EFI_ERROR("BS->ExitBootServices");


   if (ramdisk_paddr != RAMDISK_PADDR) {
      AlignedMemCpy((UINTN*)ramdisk_paddr,
                    (UINTN*)RAMDISK_PADDR,
                    RAMDISK_SIZE / sizeof(UINTN));
   }

   ((void (*)())BOOT_PADDR)();

   /* --- we should never get here in the normal case --- */

end:
   WaitForKeyPress(ST);
   return EFI_SUCCESS;
}

