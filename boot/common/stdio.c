/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>

#include "common_int.h"

#ifdef __TILCK_EFI_BOOTLOADER__
   #undef ASSERT
   #include <efi.h>
   #include <efilib.h>
   #define printk(fmt, ...) Print(CONCAT(L, fmt), ##__VA_ARGS__)
#else
   #include <tilck/common/printk.h>
#endif

int
read_line(char *buf, int buf_sz)
{
   int len = 0;
   int c;

   while (true) {

      c = intf->read_key();

      if (c == '\r' || c == '\n') {
         printk("\n");
         break;
      }

      if (!isprint(c)) {

         if (c == '\b' && len > 0) {
            printk("\b \b");
            len--;
         }

         continue;
      }

      if (len < buf_sz - 1) {
         printk("%c", c);
         buf[len++] = c;
      }
   }

   buf[len] = 0;
   return len;
}
