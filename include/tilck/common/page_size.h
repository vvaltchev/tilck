/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if defined(__i386__) || defined(__x86_64__)

   #define PAGE_SHIFT 12u

   #if !defined(PAGE_SIZE) || (defined(PAGE_SIZE) && PAGE_SIZE == 4096)
      #undef PAGE_SIZE
      #define PAGE_SIZE 4096u
   #else
      #error PAGE_SIZE already defined with different value
   #endif

#else

   #error Unsupported architecture.

#endif
