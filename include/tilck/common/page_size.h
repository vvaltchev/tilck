/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if defined(__i386__) || defined(__x86_64__)

   #define PAGE_SHIFT           12u
   #define PAGE_SIZE          4096u

#else

   #error Unsupported architecture.

#endif
