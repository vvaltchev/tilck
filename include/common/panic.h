
#pragma once

extern volatile bool __in_panic;

NORETURN void panic(const char *fmt, ...);
NORETURN void assert_failed(const char *expr, const char *file, int line);
NORETURN void not_reached(const char *file, int line);

static ALWAYS_INLINE bool in_panic(void)
{
   return __in_panic;
}

#ifndef NDEBUG

   #ifndef NO_EXOS_ASSERT

      #define ASSERT(x)                                                    \
         do {                                                              \
            if (UNLIKELY(!(x))) {                                          \
               assert_failed(#x , __FILE__, __LINE__);                     \
            }                                                              \
         } while (0)

   #endif

   #define DEBUG_ONLY(x) x
   #define DEBUG_CHECKED_SUCCESS(x)       \
      do {                                \
         bool __checked_success = x;      \
         ASSERT(__checked_success);       \
      } while (0)

#else

   #ifndef NO_EXOS_ASSERT
      #define ASSERT(x)
   #endif

   #define DEBUG_ONLY(x)
   #define DEBUG_CHECKED_SUCCESS(x) x

#endif

/* VERIFY is like ASSERT, but is enabled on release builds as well */
#define VERIFY(x)                                                    \
   do {                                                              \
      if (UNLIKELY(!(x))) {                                          \
         assert_failed(#x , __FILE__, __LINE__);                     \
      }                                                              \
   } while (0)


#define NOT_REACHED() not_reached(__FILE__, __LINE__)
#define NOT_IMPLEMENTED() panic("Code path not implemented yet.")
