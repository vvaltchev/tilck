
#pragma once
#include <common/basic_defs.h>

static inline size_t strlen(const char *str)
{
   register u32 count asm("ecx");
   u32 unused;

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

   asm("repne scasb\n\t"
       "notl %%ecx\n\t"
       "decl %%ecx\n\t"
      : "=c" (count), "=D" (unused)
      : "a" (0), "c" (-1), "D" (str)
      : "cc");

   return count;
}

/* dest and src can overloap only partially */
static inline void memcpy(void *dest, const void *src, size_t n)
{
   u32 unused; /* See the comment in strlen() about the unused variable */

   /*
    * (Partial) No-overlap check.
    * NOTE: this check allows intentionally an overlap in the case where
    *
    *                         [ dest + n >= src ]
    *
    *    +----------------+
    *    |      DEST      |
    *    +----------------+
    *            +----------------+
    *            |       SRC      |
    *            +----------------+
    *
    * But, given the forward direction of copy this is perfectly fine.
    */
   ASSERT( dest < src || ((uptr)src + n <= (uptr)dest) );

   asmVolatile("rep movsd\n\t"         // copy 4 bytes at a time, n/4 times
               "mov %%ebx, %%ecx\n\t"  // then: ecx = ebx = n % 4
               "rep movsb\n\t"         // copy 1 byte at a time, n%4 times
               : "=b" (unused), "=c" (n), "=S" (src), "=D" (dest)
               : "b" (n & 3), "c" (n >> 2), "S"(src), "D"(dest)
               : "cc", "memory");
}

/* dest and src might overlap anyhow */
static inline void memmove(void *dest, const void *src, size_t n)
{
   if (dest < src || ((uptr)src + n <= (uptr)dest)) {

      memcpy(dest, src, n);

   } else {

      /*
       * In this case dest > src but they overlap this way:
       *
       *                   [ src + n <= dest ]
       *
       *            +----------------+
       *            |       DEST     |
       *            +----------------+
       *    +----------------+
       *    |       SRC      |
       *    +----------------+
       *
       * Using the forward direction will cause a corruption of the src buffer,
       * while, the backwards direction solves the problem.
       */

      asmVolatile ("std\n\t"
                   "rep movsb\n\t"
                   "cld\n\t"
                   : "=c" (n), "=S" (src), "=D" (dest)
                   : "c" (n), "S" (src+n-1), "D" (dest+n-1)
                   : "cc", "memory");
   }
}

