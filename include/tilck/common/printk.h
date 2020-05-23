/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#ifndef USERMODE_APP

   void vprintk(const char *fmt, va_list args);
   void printk(const char *fmt, ...);

   #ifdef __TILCK_KERNEL__

      #define PRINTK_CTRL_CHAR   '\x01'

      int vsnprintk(char *buf, size_t size, const char *fmt, va_list args);
      int snprintk(char *buf, size_t size, const char *fmt, ...);
      void printk_flush_ringbuf(void);

   #endif

   #ifndef UNIT_TEST_ENVIRONMENT
      #define NO_PREFIX          "\x01\x01\x00\x00"
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
