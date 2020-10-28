/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * NOTE: there's no #pragma once in this header. It is supposed to be included
 * more than once, when necessary.
 *
 * Q: Why having a separate header for ASSERT instead of just using panic.h ?
 *
 * A: Because an ASSERT macro is defined in gnu-efi as well. Because of that,
 *    the EFI bootloader cannot build. Currently, panic.h includes automatically
 *    assert.h in all the cases except where NO_TILCK_ASSERT is defined. Because
 *    ASSERTs are used in headers as well, in order to include gnu-efi headers
 *    is necessary to #undef ASSERT and re-define it after including the gnu-efi
 *    headers by including this header again.
 */

#include <tilck/common/basic_defs.h>

NORETURN void assert_failed(const char *expr, const char *file, int line);

#ifdef ASSERT
   #undef ASSERT
#endif

#ifndef NDEBUG

   #define ASSERT(x)                                                    \
      do {                                                              \
         if (UNLIKELY(!(x))) {                                          \
            assert_failed(#x , __FILE__, __LINE__);                     \
         }                                                              \
      } while (0)

#else

   #define ASSERT(x) do { /* nothing */ } while (0)

#endif

