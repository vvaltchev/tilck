
#include "utils.h"

#ifdef BITS32

void jump_to_kernel(multiboot_info_t *mbi, void *entry_point)
{
   /* Jump to the kernel */
   asmVolatile("jmp *%%ecx"
               : /* no output */
               : "a" (MULTIBOOT_BOOTLOADER_MAGIC),
                 "b" (mbi),
                 "c" (entry_point)
               : /* no clobber */);
}

#else

/* Defined in switchmode.S */
void switch_to_pm32_and_jump_to_kernel(multiboot_info_t *mbi, void *entry);

void jump_to_kernel(multiboot_info_t *mbi, void *entry_point)
{
   switch_to_pm32_and_jump_to_kernel(mbi, entry_point);
}

#endif

void DumpFirst16Bytes(char *buf)
{
   Print(L"First 16 bytes in hex: \r\n");
   for (int i = 0; i < 16; i++) {
      Print(L"%02x ", (unsigned char)buf[i]);
   }
   Print(L"\r\n");
}

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
