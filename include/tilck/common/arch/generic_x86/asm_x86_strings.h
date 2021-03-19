/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define BUILTIN_SIZE_THRESHOLD      16

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvla"

EXTERN inline size_t strlen(const char *str)
{
   register u32 count asm("ecx");
   ulong unused;

   /*
    * 0. ASSUME DF = 0 (the compiler assumes that already everywhere!)
    * 1. ecx = -1
    * 2. eax = 0
    * 3. edi = str
    * 4. while (ecx != 0) {
    *      ZF = (al == *(BYTE *)edi);
    *      edi++;
    *      ecx--;
    *      if (ZF) break;
    *    }
    * 5. ecx = ~ecx;
    * 6. ecx--;
    *
    * For more details: https://stackoverflow.com/questions/26783797/
    *
    * The story behind "unused": because of the way repne scasb works,
    * the pointer has to be stored in EDI, but it will also be modified by it.
    * At that point, it would sound reasonable to put EDI in clobbers, but gcc
    * does not allow that. The work-around suggested by the official
    * documentation: https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html
    * is to store the modified input register into an (unused) output variable.
    */

   asmVolatile("repne scasb\n\t"
               "notl %%ecx\n\t"
               "decl %%ecx\n\t"
               : "=c" (count), "=D" (unused)
               : "a" (0), "c" (-1), "D" (str)
               : "cc");

   return count;
}

/* dest and src can overloap only partially */
EXTERN inline void *memcpy(void *dest, const void *src, size_t n)
{
   void *saved_dest = dest;

   /*
    * (Partial) No-overlap check.
    * NOTE: this check allows intentionally an overlap in the following cases:
    *
    * 1.
    *    +----------------+
    *    |      DEST      |
    *    +----------------+
    *                        +----------------+
    *                        |       SRC      |
    *                        +----------------+
    *
    * 2.
    *    +----------------+
    *    |      DEST      |
    *    +----------------+
    *            +----------------+
    *            |       SRC      |
    *            +----------------+
    *
    * 3.
    *    +----------------+
    *    |      DEST      |
    *    +----------------+
    *    +----------+
    *    |    SRC   |
    *    +----------+
    *
    * Which can be expressed with:
    *                            [ DEST <= SRC ]
    *
    *
    * 4.
    *                          +----------------+
    *                          |       DEST     |
    *                          +----------------+
    *    +----------------+
    *    |       SRC      |
    *    +----------------+
    *
    * Which can be expressed with:
    *                            [ SRC + n <= DEST ]
    */
   ASSERT( dest <= src || ((ulong)src + n <= (ulong)dest) );

   if (__builtin_constant_p(n) && n <= BUILTIN_SIZE_THRESHOLD)
      return __builtin_memcpy(dest, src, n);

   asm("rep movsl\n\t"         // copy 4 bytes at a time, n/4 times
       "mov %k5, %%ecx\n\t"    // then: ecx = (register) = n%4
       "rep movsb\n\t"         // copy 1 byte at a time, n%4 times
       : "+D"(dest), "+S"(src), "=c"(n), "=m"(*(char (*)[n])dest)
       : "c"(n >> 2), "r"(n & 3), "m"(*(const char (*)[n])src));

   return saved_dest;
}

EXTERN inline void *memcpy32(void *dest, const void *src, size_t n)
{
   void *saved_dest = dest;
   ASSERT( dest <= src || ((ulong)src + n <= (ulong)dest) );

   asm("rep movsl\n\t"
       : "+D"(dest), "+S"(src), "=c"(n), "=m"(*(char (*)[n << 2])dest)
       : "c"(n), "m"(*(const char (*)[n << 2])src));

   return saved_dest;
}

/* dest and src might overlap anyhow */
EXTERN inline void *memmove(void *dest, const void *src, size_t n)
{
   if (__builtin_constant_p(n) && n <= BUILTIN_SIZE_THRESHOLD)
      return __builtin_memmove(dest, src, n);

   if (dest <= src || ((ulong)src + n <= (ulong)dest)) {

      memcpy(dest, src, n);

   } else {

      /*
       * In this SRC + n >= DEST:
       *
       *            +----------------+
       *            |       DEST     |
       *            +----------------+
       *    +----------------+
       *    |       SRC      |
       *    +----------------+
       *
       * Using the forward direction will cause a corruption of the SRC buffer,
       * while, the backwards direction solves the problem.
       */

      u32 unused;

      asmVolatile("std\n\t"
                  "rep movsb\n\t"
                  "cld\n\t"
                  : "=c" (n), "=S" (src), "=D" (unused)
                  : "c" (n), "S" ((char*)src+n-1), "D" ((char*)dest+n-1)
                  : "cc", "memory");
   }

   return dest;
}

/*
 * Set 'n' bytes pointed by 's' to 'c'.
 */
EXTERN inline void *memset(void *s, int c, size_t n)
{
   void *saved = s;

   if (__builtin_constant_p(n) && n <= BUILTIN_SIZE_THRESHOLD)
      return __builtin_memset(s, c, n);

   asm("rep stosb"
       : "+D" (s), "+a" (c), "+c" (n), "=m"(*(char (*)[n])s));

   return saved;
}

/*
 * Set 'n' 16-bit elems pointed by 's' to 'val'.
 */
EXTERN inline void *memset16(u16 *s, u16 val, size_t n)
{
   void *saved = s;

   asm("rep stosw"
       : "+D" (s), "+a" (val), "+c" (n), "=m"(*(char (*)[n << 1])s));

   return saved;
}

/*
 * Set 'n' 32-bit elems pointed by 's' to 'val'.
 */
EXTERN inline void *memset32(u32 *s, u32 val, size_t n)
{
   void *saved = s;

   asm("rep stosl"
       : "+D" (s), "+a" (val), "+c" (n), "=m"(*(char (*)[n << 2])s));

   return saved;
}

EXTERN inline void bzero(void *s, size_t n)
{
   ulong unused; /* See the comment in strlen() about the unused variable */

   asmVolatile("xor %%eax, %%eax\n\t"    // eax = 0
               "rep stosl\n\t"           // zero 4 byte at a time, n / 4 times
               "mov %%ebx, %%ecx\n\t"    // ecx = ebx = n % 4
               "rep stosb\n\t"           // zero 1 byte at a time, n % 3 times
               : "=D" (s), "=c" (n), "=b" (unused)
               :  "D" (s), "c" (n >> 2), "b" (n & 3)
               : "cc", "memory", "%eax");
}

#pragma GCC diagnostic pop
