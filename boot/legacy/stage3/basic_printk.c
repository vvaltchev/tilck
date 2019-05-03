/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include "basic_term.h"

static void print_string(const char *s)
{
   while (*s)
      bt_write_char(*s++);
}

void vprintk(const char *fmt, va_list args)
{
   const char *ptr = fmt;
   char buf[64];

   while (*ptr) {

      if (*ptr != '%') {
         bt_write_char(*ptr++);
         continue;
      }

      // *ptr is '%'

      ++ptr;

      if (*ptr == '%')
         continue;

      switch (*ptr) {

      case 'l':
         ++ptr;

         if (*ptr && *ptr == 'l') {
            ++ptr;
            if (*ptr) {
               if (*ptr == 'u') {
                  uitoa64_dec(va_arg(args, u64), buf);
                  print_string(buf);
               } else if (*ptr == 'i' || *ptr == 'd') {
                  itoa64(va_arg(args, s64), buf);
                  print_string(buf);
               } else if (*ptr == 'x') {
                  uitoa64_hex_fixed(va_arg(args, u64), buf);
                  print_string(buf);
               }
            }
         }
         break;

      case 'd':
      case 'i':
         itoa32(va_arg(args, s32), buf);
         print_string(buf);
         break;

      case 'u':
         uitoa32_dec(va_arg(args, u32), buf);
         print_string(buf);
         break;

      case 'x':
         uitoa32_hex(va_arg(args, u32), buf);
         print_string(buf);
         break;

      case 'c':
         bt_write_char((char)va_arg(args, int));
         break;

      case 's':
         print_string(va_arg(args, const char *));
         break;

      case 'p':
         uitoa32_hex_fixed(va_arg(args, uptr), buf);
         print_string("0x");
         print_string(buf);
         break;

      default:
         bt_write_char('%');
         bt_write_char(*ptr);
      }

      ++ptr;
   }
}

void printk(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
}

