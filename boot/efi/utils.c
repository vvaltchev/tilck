/* SPDX-License-Identifier: BSD-2-Clause */

#include "defs.h"
#include "utils.h"

#if defined(BITS32)

void JumpToKernel(multiboot_info_t *mbi, void *entry_point)
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

void JumpToKernel(multiboot_info_t *mbi, void *entry_point)
{
   switch_to_pm32_and_jump_to_kernel(mbi, entry_point);
}

#else

#error Unknown architectre

#endif

EFI_INPUT_KEY WaitForKeyPress(EFI_SYSTEM_TABLE *ST)
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

EFI_STATUS
LoadFileFromDisk(EFI_BOOT_SERVICES *BS,
                 EFI_FILE_PROTOCOL *fileProt,
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
