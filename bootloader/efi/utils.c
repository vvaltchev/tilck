
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

void bzero(void *ptr, UINTN len)
{
   for (UINTN i = 0; i < len; i++)
      ((char*)ptr)[i] = 0;
}

void DumpFirst16Bytes(char *buf)
{
   Print(L"First 16 bytes in hex: \r\n");
   for (int i = 0; i < 16; i++) {
      Print(L"%02x ", (unsigned char)buf[i]);
   }
   Print(L"\r\n");
}

void WaitForKeyPress(EFI_SYSTEM_TABLE *ST)
{
    UINTN index;
    EFI_INPUT_KEY k;
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

/* dest and src can overloap only partially */
void *my_memcpy(void *dest, const void *src, size_t n)
{
   u32 unused;
   u32 unused2;

   asmVolatile("rep movsl\n\t"         // copy 4 bytes at a time, n/4 times
               "mov %%ebx, %%ecx\n\t"  // then: ecx = ebx = n % 4
               "rep movsb\n\t"         // copy 1 byte at a time, n%4 times
               : "=b" (unused), "=c" (n), "=S" (src), "=D" (unused2)
               : "b" (n & 3), "c" (n >> 2), "S"(src), "D"(dest)
               : "cc", "memory");

   return dest;
}

/* dest and src might overlap anyhow */
void *my_memmove(void *dest, const void *src, size_t n)
{
   if (dest < src || ((uptr)src + n <= (uptr)dest)) {

      my_memcpy(dest, src, n);

   } else {

      u32 unused;

      asmVolatile("std\n\t"
                  "rep movsb\n\t"
                  "cld\n\t"
                  : "=c" (n), "=S" (src), "=D" (unused)
                  : "c" (n), "S" (src+n-1), "D" (dest+n-1)
                  : "cc", "memory");
   }

   return dest;
}

