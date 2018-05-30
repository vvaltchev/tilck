
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <exos/arch/generic_x86/fpu_memcpy.h>

void init_fpu_memcpy(void)
{
   /* Do nothing, for the moment. */
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
