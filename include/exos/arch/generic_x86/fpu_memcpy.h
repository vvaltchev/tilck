
#pragma once
#include <common/basic_defs.h>
#include <common/string_util.h>

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

static ALWAYS_INLINE void
fpu_memcpy_single_512_nt_avx2(void *dest, const void *src)
{
   asmVolatile("vmovdqa   (%0), %%ymm0\n\t"
               "vmovdqa 32(%0), %%ymm1\n\t"
               "vmovntdq %%ymm0,   (%1)\n\t"
               "vmovntdq %%ymm1, 32(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

static ALWAYS_INLINE void
fpu_memcpy_single_256_nt_avx2(void *dest, const void *src)
{
   asmVolatile("vmovdqa   (%0), %%ymm0\n\t"
               "vmovntdq %%ymm0,   (%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}


static ALWAYS_INLINE void
fpu_memcpy_single_512_nt_sse2(void *dest, const void *src)
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

static ALWAYS_INLINE void
fpu_memcpy_single_256_nt_sse2(void *dest, const void *src)
{
   asmVolatile("movdqa   (%0), %%xmm0\n\t"
               "movdqa 16(%0), %%xmm1\n\t"
               "movntdq %%xmm0,   (%1)\n\t"
               "movntdq %%xmm1, 16(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

static ALWAYS_INLINE void
fpu_memcpy_single_128_nt_sse2(void *dest, const void *src)
{
   asmVolatile("movdqa   (%0), %%xmm0\n\t"
               "movntdq %%xmm0,   (%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

static ALWAYS_INLINE void
fpu_memcpy_single_256_nt_sse(void *dest, const void *src)
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

static ALWAYS_INLINE void
fpu_memcpy_single_128_nt_sse(void *dest, const void *src)
{
   asmVolatile("movq (%0), %%mm0\n\t"
               "movq 8(%0), %%mm1\n\t"
               "movntq %%mm0, (%1)\n\t"
               "movntq %%mm1, 8(%1)\n\t"
               : /* no output */
               : "r" (src), "r" (dest)
               : "memory");
}

static ALWAYS_INLINE void
memcpy256_failsafe(void *dest, const void *src, u32 n)
{
   memcpy32(dest, src, n * 8);
}

static ALWAYS_INLINE void
memcpy_single_256_failsafe(void *dest, const void *src)
{
   memcpy32(dest, src, 8);
}


/* 'n' is the number of 32-byte (256-bit) data packets to copy */
static ALWAYS_INLINE void
fpu_memcpy256_nt(void *dest, const void *src, u32 n)
{
   /* Note: using IFs this way is faster than using a function pointer. */
   if (x86_cpu_features.can_use_avx2)
      fpu_memcpy256_nt_avx2(dest, src, n);
   else if (x86_cpu_features.can_use_sse2)
      fpu_memcpy256_nt_sse2(dest, src, n);
   else if (x86_cpu_features.can_use_sse)
      fpu_memcpy256_nt_sse(dest, src, n);
   else
      memcpy256_failsafe(dest, src, n);
}

static ALWAYS_INLINE void
fpu_memcpy_single_256_nt(void *dest, const void *src)
{
   /* Note: using IFs this way is faster than using a function pointer. */
   if (x86_cpu_features.can_use_avx2)
      fpu_memcpy_single_256_nt_avx2(dest, src);
   else if (x86_cpu_features.can_use_sse2)
      fpu_memcpy_single_256_nt_sse2(dest, src);
   else if (x86_cpu_features.can_use_sse)
      fpu_memcpy_single_256_nt_sse(dest, src);
   else
      memcpy_single_256_failsafe(dest, src);
}

void init_fpu_memcpy(void);
