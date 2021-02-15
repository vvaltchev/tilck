/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

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
   u32 unused; /* See the comment in strlen() about the unused variable */
   u32 unused2;

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

   asmVolatile("rep movsl\n\t"         // copy 4 bytes at a time, n/4 times
               "mov %%ebx, %%ecx\n\t"  // then: ecx = ebx = n % 4
               "rep movsb\n\t"         // copy 1 byte at a time, n%4 times
               : "=b" (unused), "=c" (n), "=S" (src), "=D" (unused2)
               : "b" (n & 3), "c" (n >> 2), "S"(src), "D"(dest)
               : "cc", "memory");

   return dest;
}

EXTERN inline void *memcpy32(void *dest, const void *src, size_t n)
{
   u32 unused;
   ASSERT( dest <= src || ((ulong)src + n <= (ulong)dest) );

   asmVolatile("rep movsl\n\t"         // copy 4 bytes at a time, n times
               : "=c" (n), "=S" (src), "=D" (unused)
               : "c" (n), "S"(src), "D"(dest)
               : "cc", "memory");

   return dest;
}

/* dest and src might overlap anyhow */
EXTERN inline void *memmove(void *dest, const void *src, size_t n)
{
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
   ulong unused; /* See the comment in strlen() about the unused variable */

   asmVolatile("rep stosb"
               : "=D" (unused), "=a" (c), "=c" (n)
               :  "D" (s), "a" (c), "c" (n)
               : "cc", "memory");

   return s;
}

/*
 * Set 'n' 16-bit elems pointed by 's' to 'val'.
 */
EXTERN inline void *memset16(u16 *s, u16 val, size_t n)
{
   ulong unused; /* See the comment in strlen() about the unused variable */

   asmVolatile("rep stosw"
               : "=D" (unused), "=a" (val), "=c" (n)
               :  "D" (s), "a" (val), "c" (n)
               : "cc", "memory");

   return s;
}

/*
 * Set 'n' 32-bit elems pointed by 's' to 'val'.
 */
EXTERN inline void *memset32(u32 *s, u32 val, size_t n)
{
   ulong unused; /* See the comment in strlen() about the unused variable */

   asmVolatile("rep stosl"
               : "=D" (unused), "=a" (val), "=c" (n)
               :  "D" (s), "a" (val), "c" (n)
               : "cc", "memory");

   return s;
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

