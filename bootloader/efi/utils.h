
#include <exos/common/basic_defs.h>

#include <efi.h>
#include <efilib.h>
#include <multiboot.h>

/*
 * Checks if 'addr' is in the range [begin, end).
 */
#define IN_RANGE(addr, begin, end) ((begin) <= (addr) && (addr) < (end))

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

EFI_INPUT_KEY WaitForKeyPress(EFI_SYSTEM_TABLE *ST);

void bzero(void *ptr, UINTN len);
void DumpFirst16Bytes(char *buf);
void *my_memcpy(void *dest, const void *src, size_t n);
void *my_memmove(void *dest, const void *src, size_t n);
void jump_to_kernel(multiboot_info_t *mbi, void *entry_point);
