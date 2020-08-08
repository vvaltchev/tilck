/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include "basic_term.h"

/* This extra-limited printk implementation treats %ld as %d */
STATIC_ASSERT(sizeof(long) == sizeof(int));

static void print_string(const char *s)
{
   while (*s)
      bt_write_char(*s++);
}

static void print_ll(char fmtX, char *buf, u64 val)
{
   switch (fmtX) {

      case 'i':
      case 'd':
         itoa64((s64)val, buf);
         break;

      case 'u':
         uitoa64(val, buf, 10);
         break;

      case 'x':
         uitoa64_hex_fixed(val, buf);
         break;

      default:
         buf[0] = 0;
   }

   if (buf[0])
      print_string(buf);
}

void vprintk(const char *fmt, va_list args)
{
   char buf[64];

   for (const char *ptr = fmt; *ptr; ptr++) {

      if (*ptr != '%') {
         bt_write_char(*ptr);
         continue;
      }

      /* OK, we read '%', forward by one */
      ptr++;

      switch (*ptr) {

      case '%':
         bt_write_char(*ptr);
         break;

      case 'l':

         /* Just skip 'l', treating %ld and %d the same way */
         ptr++;

         if (!*ptr)
            return;

         if (*ptr == 'l') {

            /* OK, we've got %ll */
            ptr++;

            if (!*ptr)
               return;

            print_ll(*ptr, buf, va_arg(args, u64));
         }
         break;

      case 'd':
      case 'i':
         itoa32(va_arg(args, s32), buf);
         print_string(buf);
         break;

      case 'u':
         uitoa32(va_arg(args, u32), buf, 10);
         print_string(buf);
         break;

      case 'x':
         uitoa32(va_arg(args, u32), buf, 16);
         print_string(buf);
         break;

      case 'c':
         bt_write_char((char)va_arg(args, int));
         break;

      case 's':
         print_string(va_arg(args, const char *));
         break;

      case 'p':
         uitoaN_hex_fixed(va_arg(args, ulong), buf);
         print_string("0x");
         print_string(buf);
         break;

      default:
         bt_write_char('%');
         bt_write_char(*ptr);
      }
   }
}

void printk(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
}

