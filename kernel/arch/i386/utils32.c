
#include <string_util.h>
#include <hal.h>
#include <process.h>

void memcpy(void *dest, const void *src, size_t n)
{
   // Copy byte-by-byte until 'b' because divisible by 4
   asmVolatile ("cld\n\t"
                "rep movsb\n\t"
                : // no output
                :"c" (n % 4),"S" (src),"D" (dest)
                :"memory");

   // Copy a dword at time the rest
   asmVolatile ("rep movsd\n\t"
                : // no output
                : "c" (n / 4)
                : "memory");
}

// Dest and src can overlap
void memmove(void *dest, const void *src, size_t n)
{
   if (dest < src) {

      memcpy(dest, src, n);

   } else {
      asmVolatile ("std\n\t"
                   "rep movsb\n\t"
                   : // no output
                   :"c" (n), "S" (src+n-1), "D" (dest+n-1)
                   :"memory");
   }
}
