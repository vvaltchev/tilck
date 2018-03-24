
#pragma once
#include <common/basic_defs.h>

static inline size_t strlen(const char *str)
{
   register u32 count asm("ecx");
   u32 unused_var;

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
    * The story behind "unused_var": because of the way repne scasb works,
    * the pointer has to be stored in EDI, but it will also be modified by it.
    * At that point, it would sound reasonable to put EDI in clobbers, but gcc
    * does not allow that. The work-around suggested by the official
    * documentation: https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html
    * is to store the modified input register into an (unused) output variable.
    */

   asm("repne scasb\n\t"
       "notl %%ecx\n\t"
       "decl %%ecx\n\t"
      : "=c" (count), "=D" (unused_var)
      : "a" (0), "c" (-1), "D" (str)
      : "cc");

   return count;
}
