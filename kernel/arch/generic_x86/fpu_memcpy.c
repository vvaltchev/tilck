
#include <common/basic_defs.h>
#include <common/string_util.h>

fpu_memcpy_type fpu_memcpy_type_to_use;

void init_fpu_memcpy(void)
{
   if (x86_cpu_features.can_use_avx2)
      fpu_memcpy_type_to_use = FPU_MEMCPY_avx2;
   else if (x86_cpu_features.can_use_sse2)
      fpu_memcpy_type_to_use = FPU_MEMCPY_sse2;
   else if (x86_cpu_features.can_use_sse)
      fpu_memcpy_type_to_use = FPU_MEMCPY_sse;
   else
      fpu_memcpy_type_to_use = FPU_MEMCPY_failsafe;
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
