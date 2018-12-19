#pragma once

#include <tilck/common/basic_defs.h>

#if defined(UNIT_TEST_ENVIRONMENT) && \
    !defined(__clang__)  &&           \
    defined(__GNUC__) &&              \
    __GNUC__ <= 4 &&                  \
    __GNUC_MINOR__ <= 8

/*
 * Because of Travis' old compiler (GCC 4.8), that we use to build the
 * 64-bit kernel_noarch_static_for_test target, does not support C11's atomics
 * we have to implement them somehow, EVEN IF the implementation is not
 * efficient. On all the other environments, we use a newer GCC so that the
 * unit tests can actually test kernel's code with the proper C11 atomics.
 *
 * NOTE: given the #if above, in NO CASE the real kernel code (not for testing)
 * will be using the "trivial" atomic functions defined here, because the
 * compilation will simply fail if <stdatomic.h> does not exist.
 */

#define atomic_compare_exchange_strong_explicit(ptr, expval, newval, m1, m2) \
   __sync_bool_compare_and_swap(ptr, *(expval), newval)

#define atomic_compare_exchange_weak_explicit(ptr, expval, newval, m1, m2) \
   __sync_bool_compare_and_swap(ptr, *(expval), newval)

#define atomic_exchange_explicit(ptr, newval, mo)                      \
   ({                                                                  \
      typeof(*(ptr)) __old_v;                                          \
      do {                                                             \
         __old_v = *(ptr);                                             \
      } while (!__sync_bool_compare_and_swap(ptr, __old_v, newval));   \
      __old_v;                                                         \
   })

#define atomic_store_explicit(ptr, newval, mo) \
   (void)atomic_exchange_explicit(ptr, newval, mo)

#define atomic_load_explicit(ptr, mo) \
   __sync_fetch_and_add(ptr, 0)

#else

#include <stdatomic.h> // system header

STATIC_ASSERT(ATOMIC_BOOL_LOCK_FREE == 2);
STATIC_ASSERT(ATOMIC_CHAR_LOCK_FREE == 2);
STATIC_ASSERT(ATOMIC_SHORT_LOCK_FREE == 2);
STATIC_ASSERT(ATOMIC_INT_LOCK_FREE == 2);
STATIC_ASSERT(ATOMIC_LONG_LOCK_FREE == 2);
STATIC_ASSERT(ATOMIC_LLONG_LOCK_FREE == 2);
STATIC_ASSERT(ATOMIC_POINTER_LOCK_FREE == 2);

#endif // #if defined(UNIT_TEST_ENVIRONMENT) && ...

