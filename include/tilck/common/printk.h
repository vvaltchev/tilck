/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

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
