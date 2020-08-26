/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#ifndef USERMODE_APP

   #define PRINTK_FL_DEFAULT                    0
   #define PRINTK_FL_NO_PREFIX                  1

   ATTR_PRINTF_LIKE(1)
   void printk(const char *fmt, ...);

   #if defined(__TILCK_KERNEL__) || defined(UNIT_TEST_ENVIRONMENT)

      #define PRINTK_CTRL_CHAR   '\x01'

      void tilck_vprintk(u32 flags, const char *fmt, va_list args);
      static inline void vprintk(const char *fmt, va_list args) {
         tilck_vprintk(PRINTK_FL_DEFAULT, fmt, args);
      }

      int vsnprintk(char *buf, size_t size, const char *fmt, va_list args);
      int snprintk(char *buf, size_t size, const char *fmt, ...);
      void printk_flush_ringbuf(void);

   #else

      /* Legacy bootloader's basic_printk doesn't have `flags` */
      void vprintk(const char *fmt, va_list args);

   #endif

   #ifndef UNIT_TEST_ENVIRONMENT
      #define NO_PREFIX          "\x01\x01\x20\x20"
   #else
      #define NO_PREFIX          ""
   #endif

#else

   /*
   * Code in `common/` such as fat32_base.c is used also in user apps like
   * fathack.c and it will be great to be able to printk there as well instead
   * of using dirty #ifdef USERMODE_APP switches to decide whether to use printk
   * or printf.
   */

   #include <stdio.h>
   #define printk printf
   #define vprintk vprintf

   /*
    * NOTE: not including snprintk() because it's available only for the kernel
    * itself, not in the `common` code. See basic_printk.c, the one used for the
    * `common` code.
    */

#endif // #ifndef USERMODE_APP
