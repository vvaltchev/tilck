
#include <string_util.h>
#include <hal.h>

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

/*
 * Sets timer's frequency.
 * Default value: 18.222 Hz.
 */

void set_timer_freq(int hz)
{
   ASSERT(hz >= 1 && hz <= 1000);

   int divisor = 1193180 / hz;   /* Calculate our divisor */
   outb(0x43, 0x36);             /* Set our command byte 0x36 */
   outb(0x40, divisor & 0xFF);   /* Set low byte of divisor */
   outb(0x40, divisor >> 8);     /* Set high byte of divisor */
}
