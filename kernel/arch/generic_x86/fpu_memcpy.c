
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/debug_utils.h>
#include <exos/arch/generic_x86/fpu_memcpy.h>

void
memcpy256_failsafe(void *dest, const void *src, u32 n)
{
   memcpy32(dest, src, n * 8);
}

void
memcpy_single_256_failsafe(void *dest, const void *src)
{
   memcpy32(dest, src, 8);
}

/* 'n' is the number of 32-byte (256-bit) data packets to copy */
void fpu_memcpy256_nt_avx2(void *dest, const void *src, u32 n)
{
   u32 len64 = n / 2;

   for (register u32 i = 0; i < len64; i++, src += 64, dest += 64)
      fpu_memcpy_single_512_nt_avx2(dest, src);

   if (n % 2)
      fpu_memcpy_single_256_nt_avx2(dest, src);
}

/* 'n' is the number of 32-byte (256-bit) data packets to copy */
void fpu_memcpy256_nt_sse2(void *dest, const void *src, u32 n)
{
   u32 len64 = n / 2;

   for (register u32 i = 0; i < len64; i++, src += 64, dest += 64)
      fpu_memcpy_single_512_nt_sse2(dest, src);

   if (n % 2)
      fpu_memcpy_single_256_nt_sse2(dest, src);
}

/* 'n' is the number of 32-byte (256-bit) data packets to copy */
void fpu_memcpy256_nt_sse(void *dest, const void *src, u32 n)
{
   for (register u32 i = 0; i < n; i++, src += 32, dest += 32)
      fpu_memcpy_single_256_nt_sse(dest, src);
}

void init_fpu_memcpy(void)
{
   const char *func_name;
   ptrdiff_t offset;
   u32 size;
   void *func;

   func = &memcpy_single_256_failsafe;

   if (x86_cpu_features.can_use_avx2)
      func = &fpu_memcpy_single_256_nt_avx2;
   else if (x86_cpu_features.can_use_sse2)
      func = &fpu_memcpy_single_256_nt_sse2;
   else if (x86_cpu_features.can_use_sse)
      func = &fpu_memcpy_single_256_nt_sse;

   func_name = find_sym_at_addr((uptr)func, &offset, &size);

   if (!func_name) {
      panic("init_fpu_memcpy: failed to find the symbol at %p\n", func);
      return;
   }

   if (size > 128) {
      panic("init_fpu_memcpy: the source function at %p is too big!\n", func);
      return;
   }

   memcpy(&fpu_memcpy_single_256_nt, func, size);
}
