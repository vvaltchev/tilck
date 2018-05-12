
#include <efi.h>
#include <efilib.h>

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

void bzero(void *ptr, UINTN len);
void DumpFirst16Bytes(char *buf);
void WaitForKeyPress(EFI_SYSTEM_TABLE *ST);
void *my_memcpy(void *dest, const void *src, size_t n);
void *my_memmove(void *dest, const void *src, size_t n);

