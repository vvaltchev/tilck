#include <efi.h>
#include <efilib.h>

#include "efibind.h"
#include "efidef.h"
#include "efidevp.h"
#include "eficon.h"
#include "efiapi.h"
#include "efierr.h"
#include "efiprot.h"

#include <elf.h>
#include <common/config.h>

#include "utils.h"

#define PAGE_SIZE            0x1000    // 4 KB
#define BOOT_PADDR           0xC000
#define KERNEL_FILE      L"\\EFI\\BOOT\\elf_kernel_stripped"

#define DESIRED_RES_X 800
#define DESIRED_RES_Y 600


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

EFI_STATUS
LoadElfKernel(EFI_BOOT_SERVICES *BS, EFI_FILE_PROTOCOL *fileProt)
{
   EFI_STATUS status = EFI_LOAD_ERROR;
   UINTN temp_kernel_addr = KERNEL_PADDR+KERNEL_MAX_SIZE*4;
   UINTN kernel_paddr = KERNEL_PADDR;

   /*
    * Temporary load the whole kernel file in a safe location.
    */
   status = LoadFileFromDisk(BS, fileProt, KERNEL_MAX_SIZE / PAGE_SIZE,
                             temp_kernel_addr, KERNEL_FILE);
   HANDLE_EFI_ERROR("LoadFileFromDisk");

   Print(L"Kernel loaded in temporary paddr.\n");
   Print(L"Allocating memory for final kernel's location...\n");

   status = uefi_call_wrapper(BS->AllocatePages,
                              4,
                              AllocateAddress,
                              EfiBootServicesData,
                              KERNEL_MAX_SIZE / PAGE_SIZE,
                              &kernel_paddr);
   HANDLE_EFI_ERROR("AllocatePages");
   Print(L"Memory allocated.\n");

   CHECK(kernel_paddr == KERNEL_PADDR);

   Elf32_Ehdr *header = (Elf32_Ehdr *)temp_kernel_addr;

   CHECK(header->e_ident[EI_MAG0] == ELFMAG0);
   CHECK(header->e_ident[EI_MAG1] == ELFMAG1);
   CHECK(header->e_ident[EI_MAG2] == ELFMAG2);
   CHECK(header->e_ident[EI_MAG3] == ELFMAG3);
   CHECK(header->e_ehsize == sizeof(*header));

   Elf32_Phdr *phdr = (Elf32_Phdr *)(header + 1);

   Print(L"Num of program headers: %d\n", header->e_phnum);

   for (int i = 0; i < header->e_phnum; i++, phdr++) {

      // Ignore non-load segments.
      if (phdr->p_type != PT_LOAD) {
         continue;
      }

      CHECK(phdr->p_vaddr >= KERNEL_BASE_VA);

      bzero((void *)(UINTN)phdr->p_paddr, phdr->p_memsz);

      my_memmove((void *)(UINTN)phdr->p_paddr,
                 (char *) header + phdr->p_offset,
                 phdr->p_filesz);
   }

   Print(L"ELF kernel loaded\n");
   status = EFI_SUCCESS;

end:
   return status;
}

void *saved_fb_addr;
UINTN saved_fb_size;
EFI_GRAPHICS_OUTPUT_MODE_INFORMATION saved_mode_info;

void print_mode_info(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode)
{
   saved_mode_info = *mode->Info;
   saved_fb_addr = (void *)mode->FrameBufferBase;
   saved_fb_size = mode->FrameBufferSize;

   Print(L"Framebuffer addr: 0x%x\n", mode->FrameBufferBase);
   Print(L"Framebuffer size: %u\n", mode->FrameBufferSize);
   Print(L"Resolution: %u x %u\n",
         mode->Info->HorizontalResolution,
         mode->Info->VerticalResolution);

   if (mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
      Print(L"PixelFormat: RGB + reserved\n");
   else if (mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
      Print(L"PixelFormat: BGR + reserved\n");
   else
      Print(L"PixelFormat: other\n");
}

bool is_pixelformat_supported(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info)
{
   return mode_info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ||
          mode_info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor;
}

EFI_STATUS
SetupGraphicMode(EFI_BOOT_SERVICES *BS)
{
   UINTN status = EFI_SUCCESS;

   EFI_HANDLE handles[32];
   UINTN handles_buf_size;
   UINTN handles_count;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *graphicsProtocol;

   handles_buf_size = sizeof(handles);

   status = uefi_call_wrapper(BS->LocateHandle,
                              5,
                              ByProtocol,
                              &GraphicsOutputProtocol,
                              NULL,
                              &handles_buf_size,
                              handles);

   HANDLE_EFI_ERROR("LocateHandle() failed");

   handles_count = handles_buf_size/sizeof(EFI_HANDLE);

   CHECK(handles_count > 0);

   status = uefi_call_wrapper(BS->HandleProtocol,
                              3,
                              handles[0],
                              &GraphicsOutputProtocol,
                              &graphicsProtocol);

   HANDLE_EFI_ERROR("HandleProtocol() failed");

   EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode = graphicsProtocol->Mode;

   print_mode_info(mode);

   UINTN wanted_mode = (UINTN)-1;
   UINTN orig_mode = mode->Mode;

   for (UINTN i = 0; i < mode->MaxMode; i++) {

      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info = NULL;
      UINTN sizeof_info = 0;

      status = uefi_call_wrapper(graphicsProtocol->QueryMode,
                                 4,
                                 graphicsProtocol,
                                 i,
                                 &sizeof_info,
                                 &mode_info);

      HANDLE_EFI_ERROR("QueryMode() failed");

      // Print(L"Mode [%u]: %u x %u\n",
      //       i,
      //       mode_info->HorizontalResolution,
      //       mode_info->VerticalResolution);

      if (mode_info->HorizontalResolution == DESIRED_RES_X &&
          mode_info->VerticalResolution == DESIRED_RES_Y &&
          is_pixelformat_supported(mode_info)) {

         wanted_mode = i;
      }
   }

   if (wanted_mode == (UINTN)-1) {
      Print(L"Wanted mode %u x %u NOT AVAILABLE.\n",
            DESIRED_RES_X, DESIRED_RES_Y);
      status = EFI_LOAD_ERROR;
      goto end;
   }

   Print(L"About to switch to mode %u [%u x %u]. Press any key\n",
         wanted_mode, DESIRED_RES_X, DESIRED_RES_Y);
   WaitForKeyPress(ST);

   status = uefi_call_wrapper(ST->ConOut->ClearScreen,
                              1,
                              ST->ConOut);

   HANDLE_EFI_ERROR("ClearScreen() failed");

   status = uefi_call_wrapper(graphicsProtocol->SetMode,
                              2,
                              graphicsProtocol,
                              wanted_mode);

   if (EFI_ERROR(status)) {
      status = uefi_call_wrapper(graphicsProtocol->SetMode,
                                 2,
                                 graphicsProtocol,
                                 orig_mode);

      status = uefi_call_wrapper(ST->ConOut->ClearScreen,
                                 1,
                                 ST->ConOut);

      HANDLE_EFI_ERROR("ClearScreen() failed");

      Print(L"Loader failed: unable to set desired mode\n");
      status = EFI_LOAD_ERROR;
   }

   print_mode_info(mode);

end:
   return status;
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

   SetupGraphicMode(BS);
   HANDLE_EFI_ERROR("SetupGraphicMode() failed");

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
   //Print(L"Read OK\r\n");
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

   status = LoadElfKernel(BS, fileProt);
   HANDLE_EFI_ERROR("LoadElfKernel");

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
      my_memmove((UINTN*)RAMDISK_PADDR,
                 (UINTN*)ramdisk_paddr,
                  RAMDISK_SIZE);
   }

   ((void (*)())BOOT_PADDR)();

   /* --- we should never get here in the normal case --- */

end:
   WaitForKeyPress(ST);
   return EFI_SUCCESS;
}

