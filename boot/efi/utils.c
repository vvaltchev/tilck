/* SPDX-License-Identifier: BSD-2-Clause */

#include "defs.h"
#include "utils.h"

#if defined(BITS32)

void JumpToKernel(void *entry_point)
{
   /* Jump to the kernel */
   asmVolatile("jmp *%%ecx"
               : /* no output */
               : "a" (MULTIBOOT_BOOTLOADER_MAGIC),
                 "b" (mbi),
                 "c" (entry_point)
               : /* no clobber */);
}

#elif defined(BITS64)

/* Defined in switchmode.S */
void switch_to_pm32_and_jump_to_kernel(multiboot_info_t *mbi, void *entry);

void JumpToKernel(void *entry_point)
{
   switch_to_pm32_and_jump_to_kernel(mbi, entry_point);
}

#else

#error Unknown architectre

#endif

EFI_INPUT_KEY
WaitForKeyPress(void)
{
    UINTN index;
    EFI_INPUT_KEY k;
    EFI_EVENT event = ST->ConIn->WaitForKey;
    BS->WaitForEvent(1,       // number of events in the array pointed by &event
                     &event,  // pointer to events array (1 elem in our case).
                     &index); // index of the last matching event in the array

    // Read the key, allowing WaitForKey to block again.
    ST->ConIn->ReadKeyStroke(ST->ConIn, &k);
    return k;
}

int
ReadAsciiLine(char *buf, int buf_sz)
{
   EFI_INPUT_KEY k;
   int len = 0;
   CHAR16 uc;

   while (true) {

      k = WaitForKeyPress();
      uc = k.UnicodeChar;

      if (uc == '\n' || uc == '\r') {
         Print(L"\r\n");
         break;
      }

      if (!isprint(uc)) {

         if (uc == '\b' && len > 0) {
            Print(L"\b \b");
            len--;
         }

         continue;
      }

      if (len < buf_sz - 1) {
         Print(L"%c", uc);
         buf[len++] = (char)uc;
      }
   }

   buf[len] = 0;
   return len;
}

EFI_STATUS
LoadFileFromDisk(EFI_FILE_PROTOCOL *fileProt,
                 INTN pagesCount,
                 EFI_PHYSICAL_ADDRESS *paddr,
                 CHAR16 *filePath)
{
   EFI_STATUS status = EFI_SUCCESS;
   EFI_FILE_PROTOCOL *fileHandle;
   UINTN bufSize = pagesCount * PAGE_SIZE;

   status = BS->AllocatePages(AllocateAnyPages,
                              EfiLoaderData,
                              pagesCount,
                              paddr);
   HANDLE_EFI_ERROR("AllocatePages");

   status =
      fileProt->Open(fileProt, &fileHandle, filePath, EFI_FILE_MODE_READ, 0);
   HANDLE_EFI_ERROR("fileProt->Open");

   status = fileProt->Read(fileHandle, &bufSize, TO_PTR(*paddr));
   HANDLE_EFI_ERROR("fileProt->Read");

   // UINT32 crc32 = 0;
   // Print(L"Size read: %d\r\n", bufSize);
   // BS->CalculateCrc32((void*)(UINTN)paddr, bufSize, &crc32);
   // Print(L"Crc32: 0x%x\r\n", crc32);

   status = fileHandle->Close(fileHandle);
   HANDLE_EFI_ERROR("fileHandle->Close");

end:
   return status;
}

EFI_STATUS
GetMemoryMap(UINTN *mapkey)
{
   UINT32 desc_ver;
   EFI_STATUS status;

   mmap_size = sizeof(mmap);
   status = BS->GetMemoryMap(&mmap_size, mmap, mapkey, &desc_size, &desc_ver);
   HANDLE_EFI_ERROR("BS->GetMemoryMap");

end:
   return status;
}

EFI_MEMORY_DESCRIPTOR *
GetMemDescForAddress(EFI_PHYSICAL_ADDRESS paddr)
{
   EFI_MEMORY_DESCRIPTOR *desc = NULL;
   desc = (void *)mmap;

   do {

      UINT64 start = desc->PhysicalStart;
      UINT64 end = start + desc->NumberOfPages * PAGE_SIZE;

      if (IN_RANGE(paddr, start, end))
         return desc;

      desc = (void *)desc + desc_size;

   } while ((UINTN)desc < (UINTN)mmap + mmap_size);

   return NULL;
}

/*
 * ShowProgress
 *    -- Shows (curr/tot)% on the row `CurrRow` with the given prefix
 *
 * Unfortunately, the `CurrRow` parameter is not avoidable because
 * EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL has a SetCursorPosition() but NOTHING like a
 * GetCursorPosition(). It seems like we're supposed to manually track cursor's
 * position. To do that, we'll need to write a non-trivial wrapper of the
 * Print() function, calling directly OutputString(), which have to track
 * accurately the current row number. That means increasing it every time a \n
 * is found or the text overflows the current row. It will end up as a whole
 * layer on the top of EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.
 *
 * A probably better approach will be to track the current row and to call
 * SetCursorPosition() every time before Print() [still, by introducing a
 * wrapper], instead of just passively tracking it. But, in that case too we
 * must handle the case where the buffer overflows the current row.
 *
 * So, at the end, the simpler thing for the moment allowing us to show the
 * progress% while loading the ramdisk is just to hard-code the current row in
 * efi_main() and pass it down the whole way to this function, ShowProgress().
 */

void
ShowProgress(SIMPLE_TEXT_OUTPUT_INTERFACE *ConOut,
             UINTN CurrRow,            /* HACK: see the note above */
             const CHAR16 *PrefixStr,
             UINTN curr,
             UINTN tot)
{
   ConOut->SetCursorPosition(ConOut, 0, CurrRow);
   Print(L"%s%u%%", PrefixStr, 100 * curr / tot);
}


EFI_STATUS
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

EFI_STATUS
ReadDiskWithProgress(SIMPLE_TEXT_OUTPUT_INTERFACE *ConOut,
                     UINTN CurrRow,
                     const CHAR16 *loadingStr,
                     EFI_BLOCK_IO_PROTOCOL *blockio,
                     UINT64 Offset,
                     UINTN BufferSize,
                     void *Buffer)
{
   const UINTN ChunkSize = 256 * KB;
   const UINTN ChunkCount = BufferSize / ChunkSize;
   const UINTN rem = BufferSize - ChunkCount * ChunkSize;
   EFI_STATUS status = EFI_SUCCESS;

   for (u32 chunk = 0; chunk < ChunkCount; chunk++) {

      if (chunk > 0) {
         ShowProgress(ST->ConOut,
                      CurrRow,
                      loadingStr,
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
                loadingStr,
                BufferSize,
                BufferSize);

end:
   return status;
}

EFI_DEVICE_PATH *
DevicePathGetLastValidNode(EFI_DEVICE_PATH *dp)
{
   EFI_DEVICE_PATH *curr = dp;
   EFI_DEVICE_PATH *prev = dp;

   for (; !IsDevicePathEnd(curr); curr = NextDevicePathNode(curr)) {
      prev = curr;
   }

   return prev;
}

void
TruncateDevicePath(EFI_DEVICE_PATH *dp)
{
   EFI_DEVICE_PATH *lastDp = DevicePathGetLastValidNode(dp);
   SetDevicePathEndNode(lastDp);
}

EFI_DEVICE_PATH *
GetCopyOfParentDevicePathNode(EFI_DEVICE_PATH *dp)
{
   EFI_DEVICE_PATH *parent = DuplicateDevicePath(dp);
   TruncateDevicePath(parent);
   return parent;
}

EFI_STATUS
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
