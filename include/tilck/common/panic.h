/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#define __TILCK_PANIC_H__

NORETURN void panic(const char *fmt, ...);
NORETURN void not_reached(const char *file, int line);
NORETURN void not_implemented(const char *file, int line);

static ALWAYS_INLINE bool in_panic(void)
{
   extern volatile bool __in_panic;
   return __in_panic;
}

static ALWAYS_INLINE bool in_kernel_shutdown(void)
{
   extern volatile bool __in_kernel_shutdown;
   return __in_kernel_shutdown;
}

#ifndef NDEBUG

   #define DEBUG_ONLY(x) x
   #define DEBUG_ONLY_UNSAFE(x) x
   #define DEBUG_CHECKED_SUCCESS(x)       \
      do {                                \
         bool __checked_success = x;      \
         ASSERT(__checked_success);       \
      } while (0)

   /* NO_TEST_ASSERT is like ASSERT(x) but it's ignored in unit tests */
   #ifndef UNIT_TEST_ENVIRONMENT
      #define NO_TEST_ASSERT(x) ASSERT(x)
   #else
      #define NO_TEST_ASSERT(x) do { /* nothing */ } while (0)
   #endif

#else

   #define DEBUG_ONLY_UNSAFE(x) /* expand to nothing */
   #define DEBUG_ONLY(x) do { /* nothing */ } while (0)
   #define NO_TEST_ASSERT(x) do { /* nothing */ } while (0)
   #define DEBUG_CHECKED_SUCCESS(x) x

#endif

/* VERIFY is like ASSERT, but it's always enabled */
#define VERIFY(x)                                                    \
   do {                                                              \
      if (UNLIKELY(!(x))) {                                          \
         assert_failed(#x , __FILE__, __LINE__);                     \
      }                                                              \
   } while (0)


#define NOT_REACHED() not_reached(__FILE__, __LINE__)
#define NOT_IMPLEMENTED() not_implemented(__FILE__, __LINE__)


#ifndef NO_TILCK_ASSERT
   #include <tilck/common/assert.h>
#endif
