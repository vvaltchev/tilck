
/*
 * Dirty "kitchen sink" header used to reduce the size of efimain.c.
 *
 * TODO:
 *    - Make the efi bootloader build system to support multiple C files.
 *    - Split the functions across several C files
 *    - Make this to be a proper header file. If necessary, create additional
 *      local header files.
 */

#include <efi.h>
#include <efilib.h>

#define asmVolatile __asm__ volatile
typedef UINTN uptr;

#define CHECK(cond)                                  \
   do {                                              \
      if (!(cond)) {                                 \
         Print(L"CHECK '%a' FAILED\n", #cond);       \
         status = EFI_LOAD_ERROR;                    \
         goto end;                                   \
      }                                              \
   } while(0)

#define HANDLE_EFI_ERROR(op)                                 \
    do {                                                     \
       if (EFI_ERROR(status)) {                              \
          Print(L"[%a] Error: %r ", op, status);             \
          goto end;                                          \
       }                                                     \
    } while (0)

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
