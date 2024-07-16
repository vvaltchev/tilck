/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) \
   || defined(__riscv)

   #define PAGE_SHIFT 12ul

   #if !defined(PAGE_SIZE) || (defined(PAGE_SIZE) && PAGE_SIZE == 4096)
      #undef PAGE_SIZE
      #define PAGE_SIZE 4096ul
   #else
      #error PAGE_SIZE already defined with different value
   #endif

#else

   #error Unsupported architecture.

#endif
