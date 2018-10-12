/* SPDX-License-Identifier: BSD-2-Clause */

#define __FPU_MEMCPY_C__

/*
 * The code in this translation unit, in particular the *single* funcs defined
 * in fpu_memcpy.h have to be FAST, even in debug builds. It is important the
 * hot-patched fpu_cpy_single_256_nt, created by copying the once of the
 * fpu_cpy_*single* funcs to just execute the necessary MOVs and then just a
 * RET. No prologue/epilogue, no frame pointer, no stack variables.
 * NOTE: clearly, the code works well even if -O0 and without the PRAGMAs
 * below. Just, we want it to be fast in debug builds to improve the user
 * experience there.
 */

#if defined(__GNUC__) && !defined(__clang__)
   #pragma GCC optimize "-O3"
   #pragma GCC optimize "-fomit-frame-pointer"
#endif

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/arch/generic_x86/fpu_memcpy.h>


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
      fpu_cpy_single_512_nt_avx2(dest, src);

   if (n % 2)
      fpu_cpy_single_256_nt_avx2(dest, src);
}

/* 'n' is the number of 32-byte (256-bit) data packets to copy */
void fpu_memcpy256_nt_sse2(void *dest, const void *src, u32 n)
{
   u32 len64 = n / 2;

   for (register u32 i = 0; i < len64; i++, src += 64, dest += 64)
      fpu_cpy_single_512_nt_sse2(dest, src);

   if (n % 2)
      fpu_cpy_single_256_nt_sse2(dest, src);
}

/* 'n' is the number of 32-byte (256-bit) data packets to copy */
void fpu_memcpy256_nt_sse(void *dest, const void *src, u32 n)
{
   for (register u32 i = 0; i < n; i++, src += 32, dest += 32)
      fpu_cpy_single_256_nt_sse(dest, src);
}

/* 'n' is the number of 32-byte (256-bit) data packets to copy */
void fpu_memcpy256_avx2(void *dest, const void *src, u32 n)
{
   u32 len64 = n / 2;

   for (register u32 i = 0; i < len64; i++, src += 64, dest += 64)
      fpu_cpy_single_512_avx2(dest, src);

   if (n % 2)
      fpu_cpy_single_256_avx2(dest, src);
}

/* 'n' is the number of 32-byte (256-bit) data packets to copy */
void fpu_memcpy256_sse2(void *dest, const void *src, u32 n)
{
   u32 len64 = n / 2;

   for (register u32 i = 0; i < len64; i++, src += 64, dest += 64)
      fpu_cpy_single_512_sse2(dest, src);

   if (n % 2)
      fpu_cpy_single_256_sse2(dest, src);
}

/* 'n' is the number of 32-byte (256-bit) data packets to copy */
void fpu_memcpy256_sse(void *dest, const void *src, u32 n)
{
   for (register u32 i = 0; i < n; i++, src += 32, dest += 32)
      fpu_cpy_single_256_sse(dest, src);
}


void fpu_memcpy256_nt_read_avx2(void *dest, const void *src, u32 n)
{
   u32 len64 = n / 2;

   for (register u32 i = 0; i < len64; i++, src += 64, dest += 64)
      fpu_cpy_single_512_nt_read_avx2(dest, src);

   if (n % 2)
      fpu_cpy_single_256_nt_read_avx2(dest, src);
}

void fpu_memcpy256_nt_read_sse4_1(void *dest, const void *src, u32 n)
{
   u32 len64 = n / 2;

   for (register u32 i = 0; i < len64; i++, src += 64, dest += 64)
      fpu_cpy_single_512_nt_read_sse4_1(dest, src);

   if (n % 2)
      fpu_cpy_single_256_nt_read_sse4_1(dest, src);
}

void fpu_memset256_sse2(void *dest, u32 val32, u32 n)
{
   char val256[32] ALIGNED_AT(32);
   memset32((void *)val256, val32, 8);

   for (register u32 i = 0; i < n; i++, dest += 32)
      fpu_cpy_single_256_nt_sse2(dest, val256);
}

void fpu_memset256_avx2(void *dest, u32 val32, u32 n)
{
   char val256[32] ALIGNED_AT(32);
   memset32((void *)val256, val32, 8);

   for (register u32 i = 0; i < n; i++, dest += 32)
      fpu_cpy_single_256_nt_avx2(dest, val256);
}

static void
init_fpu_memcpy_internal_check(void *func, const char *fname, u32 size)
{
   if (!fname) {
      panic("init_fpu_memcpy: failed to find the symbol at %p\n", func);
      return;
   }

   if (size > 128) {
      panic("init_fpu_memcpy: the source function at %p is too big!\n", func);
      return;
   }
}

void init_fpu_memcpy(void)
{
   const char *func_name;
   ptrdiff_t offset;
   u32 size;
   void *func;

   func = &memcpy_single_256_failsafe;

   if (x86_cpu_features.can_use_avx2)
      func = &fpu_cpy_single_256_nt_avx2;
   else if (x86_cpu_features.can_use_sse2)
      func = &fpu_cpy_single_256_nt_sse2;
   else if (x86_cpu_features.can_use_sse)
      func = &fpu_cpy_single_256_nt_sse;

   func_name = find_sym_at_addr((uptr)func, &offset, &size);

   init_fpu_memcpy_internal_check(func, func_name, size);
   memcpy(&__asm_fpu_cpy_single_256_nt, func, size);

   // --------------------------------------------------------------

   func = &memcpy_single_256_failsafe;

   if (x86_cpu_features.can_use_avx2)
      func = &fpu_cpy_single_256_nt_read_avx2;
   else if (x86_cpu_features.can_use_sse4_1)
      func = &fpu_cpy_single_256_nt_read_sse4_1;
   else if (x86_cpu_features.can_use_sse2)
      func = &fpu_cpy_single_256_sse2;     /* no "nt" read here */
   else if (x86_cpu_features.can_use_sse)
      func = &fpu_cpy_single_256_sse;      /* no "nt" read here */

   func_name = find_sym_at_addr((uptr)func, &offset, &size);

   init_fpu_memcpy_internal_check(func, func_name, size);
   memcpy(&__asm_fpu_cpy_single_256_nt_read, func, size);
}
