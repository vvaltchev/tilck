
#pragma once
#include <common/basic_defs.h>
#include <common/string_util.h>

#ifdef __FPU_MEMCPY_C__
#define EXTERN extern
#else
#define EXTERN
#endif

/*
 * -----------------------------------------------
 *
 * Extra performant memcpy versions
 *
 * -----------------------------------------------
 */

void fpu_memcpy256_nt_avx2(void *dest, const void *src, u32 n);
void fpu_memcpy256_nt_sse2(void *dest, const void *src, u32 n);
void fpu_memcpy256_nt_sse(void *dest, const void *src, u32 n);

void fpu_memcpy256_avx2(void *dest, const void *src, u32 n);
void fpu_memcpy256_sse2(void *dest, const void *src, u32 n);
void fpu_memcpy256_sse(void *dest, const void *src, u32 n);

EXTERN ALWAYS_INLINE void
fpu_cpy_single_512_nt_avx2(void *dest, const void *src)
{
   asmVolatile("vmovdqa   (%0), %%ymm0\n\t"
               "vmovdqa 32(%0), %%ymm1\n\t"
               "vmovntdq %%ymm0,   (%1)\n\t"
               "vmovntdq %%ymm1, 32(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

EXTERN ALWAYS_INLINE void
fpu_cpy_single_512_avx2(void *dest, const void *src)
{
   asmVolatile("vmovdqa   (%0), %%ymm0\n\t"
               "vmovdqa 32(%0), %%ymm1\n\t"
               "vmovdqa %%ymm0,   (%1)\n\t"
               "vmovdqa %%ymm1, 32(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

EXTERN ALWAYS_INLINE void
fpu_cpy_single_256_nt_avx2(void *dest, const void *src)
{
   asmVolatile("vmovdqa   (%0), %%ymm0\n\t"
               "vmovntdq %%ymm0,   (%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

EXTERN ALWAYS_INLINE void
fpu_cpy_single_256_avx2(void *dest, const void *src)
{
   asmVolatile("vmovdqa   (%0), %%ymm0\n\t"
               "vmovdqa %%ymm0,   (%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

EXTERN ALWAYS_INLINE void
fpu_cpy_single_512_nt_sse2(void *dest, const void *src)
{
   asmVolatile("movdqa   (%0), %%xmm0\n\t"
               "movdqa 16(%0), %%xmm1\n\t"
               "movdqa 32(%0), %%xmm2\n\t"
               "movdqa 48(%0), %%xmm3\n\t"
               "movntdq %%xmm0,   (%1)\n\t"
               "movntdq %%xmm1, 16(%1)\n\t"
               "movntdq %%xmm2, 32(%1)\n\t"
               "movntdq %%xmm3, 48(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}


EXTERN ALWAYS_INLINE void
fpu_cpy_single_512_sse2(void *dest, const void *src)
{
   asmVolatile("movdqa   (%0), %%xmm0\n\t"
               "movdqa 16(%0), %%xmm1\n\t"
               "movdqa 32(%0), %%xmm2\n\t"
               "movdqa 48(%0), %%xmm3\n\t"
               "movdqa %%xmm0,   (%1)\n\t"
               "movdqa %%xmm1, 16(%1)\n\t"
               "movdqa %%xmm2, 32(%1)\n\t"
               "movdqa %%xmm3, 48(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

EXTERN ALWAYS_INLINE void
fpu_cpy_single_256_nt_sse2(void *dest, const void *src)
{
   asmVolatile("movdqa   (%0), %%xmm0\n\t"
               "movdqa 16(%0), %%xmm1\n\t"
               "movntdq %%xmm0,   (%1)\n\t"
               "movntdq %%xmm1, 16(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

EXTERN ALWAYS_INLINE void
fpu_cpy_single_256_sse2(void *dest, const void *src)
{
   asmVolatile("movdqa   (%0), %%xmm0\n\t"
               "movdqa 16(%0), %%xmm1\n\t"
               "movdqa %%xmm0,   (%1)\n\t"
               "movdqa %%xmm1, 16(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}


EXTERN ALWAYS_INLINE void
fpu_cpy_single_128_nt_sse2(void *dest, const void *src)
{
   asmVolatile("movdqa   (%0), %%xmm0\n\t"
               "movntdq %%xmm0,   (%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

EXTERN ALWAYS_INLINE void
fpu_cpy_single_128_sse2(void *dest, const void *src)
{
   asmVolatile("movdqa   (%0), %%xmm0\n\t"
               "movdqa %%xmm0,   (%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

EXTERN ALWAYS_INLINE void
fpu_cpy_single_256_nt_sse(void *dest, const void *src)
{
   asmVolatile("movq (%0), %%mm0\n\t"
               "movq 8(%0), %%mm1\n\t"
               "movq 16(%0), %%mm2\n\t"
               "movq 24(%0), %%mm3\n\t"
               "movntq %%mm0, (%1)\n\t"
               "movntq %%mm1, 8(%1)\n\t"
               "movntq %%mm2, 16(%1)\n\t"
               "movntq %%mm3, 24(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

EXTERN ALWAYS_INLINE void
fpu_cpy_single_256_sse(void *dest, const void *src)
{
   asmVolatile("movq (%0), %%mm0\n\t"
               "movq 8(%0), %%mm1\n\t"
               "movq 16(%0), %%mm2\n\t"
               "movq 24(%0), %%mm3\n\t"
               "movq %%mm0, (%1)\n\t"
               "movq %%mm1, 8(%1)\n\t"
               "movq %%mm2, 16(%1)\n\t"
               "movq %%mm3, 24(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

EXTERN ALWAYS_INLINE void
fpu_cpy_single_128_nt_sse(void *dest, const void *src)
{
   asmVolatile("movq (%0), %%mm0\n\t"
               "movq 8(%0), %%mm1\n\t"
               "movntq %%mm0, (%1)\n\t"
               "movntq %%mm1, 8(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

EXTERN ALWAYS_INLINE void
fpu_cpy_single_128_sse(void *dest, const void *src)
{
   asmVolatile("movq (%0), %%mm0\n\t"
               "movq 8(%0), %%mm1\n\t"
               "movq %%mm0, (%1)\n\t"
               "movq %%mm1, 8(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

void memcpy256_failsafe(void *dest, const void *src, u32 n);
void memcpy_single_256_failsafe(void *dest, const void *src);

/* 'n' is the number of 32-byte (256-bit) data packets to copy */
EXTERN inline void fpu_memcpy256_nt(void *dest, const void *src, u32 n)
{
   if (x86_cpu_features.can_use_avx2)
      fpu_memcpy256_nt_avx2(dest, src, n);
   else if (x86_cpu_features.can_use_sse2)
      fpu_memcpy256_nt_sse2(dest, src, n);
   else if (x86_cpu_features.can_use_sse)
      fpu_memcpy256_nt_sse(dest, src, n);
   else
      memcpy256_failsafe(dest, src, n);
}

/* 'n' is the number of 32-byte (256-bit) data packets to copy */
EXTERN inline void fpu_memcpy256(void *dest, const void *src, u32 n)
{
   if (x86_cpu_features.can_use_avx2)
      fpu_memcpy256_avx2(dest, src, n);
   else if (x86_cpu_features.can_use_sse2)
      fpu_memcpy256_sse2(dest, src, n);
   else if (x86_cpu_features.can_use_sse)
      fpu_memcpy256_sse(dest, src, n);
   else
      memcpy256_failsafe(dest, src, n);
}

void fpu_cpy_single_256_nt(void *dest, const void *src);
void init_fpu_memcpy(void);
