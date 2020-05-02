/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#ifndef __TILCK_ATOMICS__
   #error Never include fake_atomics.h directly. Include atomics.h.
#endif

/*
 * Fake C11 atomics implementation
 * --------------------------------
 *
 * Purpose
 * ---------
 * Make it possible the build to pass and eventually have the code using them
 * to run correctly in single-threaded mode.
 *
 * Use cases
 * ----------------
 * At the moment, in two cases: for unit tests built with an old compiler like
 * gcc 4.8 which does not support C11 atomics and for static analysis builds
 * which might require fancy tricks like using a system compiler but changing
 * it's sysroot with -isystem (option KERNEL_FORCE_TC_ISYSTEM=1): in that
 * scenario, when compiling C++ files it's not possible to include <atomic>.
 * Note: because some special static analysis builds requiring fake atomics are
 * NOT supposed to be run, that's perfectly safe.
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
 * HACK: completely unsafe, but OK as we're implementing FAKE atomics that are
 * only good enough for the build to pass and eventually some unit test code to
 * execute these functions in a single thread.
 */
#define ATOMIC(x) x

#define mo_relaxed 0
#define mo_consume 1
#define mo_acquire 2
#define mo_release 3
#define mo_acq_rel 4
#define mo_seq_cst 5
