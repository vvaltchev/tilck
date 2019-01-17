/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#ifndef __cplusplus

#if defined(UNIT_TEST_ENVIRONMENT) &&   \
    !defined(__clang__)  &&             \
    defined(__GNUC__) &&                \
    __GNUC__ <= 4 &&                    \
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

#define atomic_fetch_add_explicit(ptr, val, mo) \
   __sync_fetch_and_add(ptr, val)

#define atomic_fetch_sub_explicit(ptr, val, mo) \
   __sync_fetch_and_sub(ptr, val)


/*
 * HACK: completely unsafe, but OK for the build and run of the 64-bit gtests
 * on Travis to pass. Hopefully, Travis at some point will migrate to a newer
 * compiler (toolchain) and all the code in this case will be just dropped.
 *
 * All the other builds will use either C11 atomics or the C++11 ones, which
 * are, 100% compatible by design.
 */
#define ATOMIC(x) x

#else

/*
 * DEFAULT case: the Tilck kernel, compiled with a modern C compiler.
 */

#include <stdatomic.h> // system header

STATIC_ASSERT(ATOMIC_BOOL_LOCK_FREE == 2);
STATIC_ASSERT(ATOMIC_CHAR_LOCK_FREE == 2);
STATIC_ASSERT(ATOMIC_SHORT_LOCK_FREE == 2);
STATIC_ASSERT(ATOMIC_INT_LOCK_FREE == 2);
STATIC_ASSERT(ATOMIC_LONG_LOCK_FREE == 2);
STATIC_ASSERT(ATOMIC_POINTER_LOCK_FREE == 2);

#define ATOMIC(x) _Atomic(x)

#endif // #if defined(UNIT_TEST_ENVIRONMENT) && ...

#define mo_relaxed memory_order_relaxed
#define mo_consume memory_order_consume
#define mo_acquire memory_order_acquire
#define mo_release memory_order_release
#define mo_acq_rel memory_order_acq_rel
#define mo_seq_cst memory_order_seq_cst

#else

/*
 * If __cplusplus is defined, we must be in a extern "C" { } context here.
 * This is the case of C++ unit tests which include kernel C headers.
 * In order to include C++11's atomic header, we have to put it in an extern
 * "C++" { } block, because templates cannot have C-linkage.
 */

extern "C++" {
   #include <atomic>
}

#define ATOMIC(x) std::atomic<x>

#define mo_relaxed std::memory_order_relaxed
#define mo_consume std::memory_order_consume
#define mo_acquire std::memory_order_acquire
#define mo_release std::memory_order_release
#define mo_acq_rel std::memory_order_acq_rel
#define mo_seq_cst std::memory_order_seq_cst

#endif // #ifndef __cplusplus


#define atomic_cas_weak(p, ep, nv, m1, m2) \
   atomic_compare_exchange_weak_explicit((p), (ep), (nv), (m1), (m2))

#define atomic_cas_strong(p, ep, nv, m1, m2) \
   atomic_compare_exchange_strong_explicit((p), (ep), (nv), (m1), (m2))
