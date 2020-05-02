/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define __TILCK_ATOMICS__

#ifndef __cplusplus

   #if defined(UNIT_TEST_ENVIRONMENT) &&   \
      !defined(__clang__)  &&              \
      defined(__GNUC__) &&                 \
      __GNUC__ <= 4 &&                     \
      __GNUC_MINOR__ <= 8

      /*
       * This is NOT a real kernel build, it's OK to use the fake atomics for
       * unit tests if the compiler is too old and does not support C11 atomics.
       */
      #include <tilck/common/fake_atomics.h>

   #else

      /* DEFAULT case: the Tilck kernel, compiled with a modern C compiler. */

      #include <stdatomic.h> // system header

      STATIC_ASSERT(ATOMIC_BOOL_LOCK_FREE == 2);
      STATIC_ASSERT(ATOMIC_CHAR_LOCK_FREE == 2);
      STATIC_ASSERT(ATOMIC_SHORT_LOCK_FREE == 2);
      STATIC_ASSERT(ATOMIC_INT_LOCK_FREE == 2);
      STATIC_ASSERT(ATOMIC_LONG_LOCK_FREE == 2);
      STATIC_ASSERT(ATOMIC_POINTER_LOCK_FREE == 2);

      #define ATOMIC(x) _Atomic(x)

      /* Convenience macros */
      #define mo_relaxed memory_order_relaxed
      #define mo_consume memory_order_consume
      #define mo_acquire memory_order_acquire
      #define mo_release memory_order_release
      #define mo_acq_rel memory_order_acq_rel
      #define mo_seq_cst memory_order_seq_cst

   #endif // #if defined(UNIT_TEST_ENVIRONMENT) && ...

#else

   #if KERNEL_FORCE_TC_ISYSTEM

      /*
       * This is NOT a real kernel build. It's a static analysis build only.
       * It's OK to use the fake atomics to make the build pass.
       */

      #include <tilck/common/fake_atomics.h>

   #else

      /*
       * If __cplusplus is defined, we're likely in a extern "C" { } context
       * here. In order to include C++11's atomic header, we have to put it in
       * an extern "C++" { } block, because templates cannot have "C" linkage.
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

   #endif // #if KERNEL_FORCE_TC_ISYSTEM

#endif // #ifndef __cplusplus

/* ---- Convenience macros ---- */

#define atomic_cas_weak(p, ep, nv, m1, m2) \
   atomic_compare_exchange_weak_explicit((p), (ep), (nv), (m1), (m2))

#define atomic_cas_strong(p, ep, nv, m1, m2) \
   atomic_compare_exchange_strong_explicit((p), (ep), (nv), (m1), (m2))

/* ---- Basic reference counting ---- */

#define REF_COUNTED_OBJECT    int ref_count

#if !SLOW_DEBUG_REF_COUNT

/* Return the new value */
static ALWAYS_INLINE int __retain_obj(int *ref_count)
{
   ATOMIC(int) *atomic = (ATOMIC(int) *)ref_count;
   return atomic_fetch_add_explicit(atomic, 1, mo_relaxed) + 1;
}

/* Return the new value */
static ALWAYS_INLINE int __release_obj(int *ref_count)
{
   int old;
   ATOMIC(int) *atomic = (ATOMIC(int) *)ref_count;
   old = atomic_fetch_sub_explicit(atomic, 1, mo_relaxed);
   ASSERT(old > 0);
   return old - 1;
}

#else

int __retain_obj(int *ref_count);
int __release_obj(int *ref_count);

#endif

static ALWAYS_INLINE int __get_ref_count(int *ref_count)
{
   ATOMIC(int) *atomic = (ATOMIC(int) *)ref_count;
   return atomic_load_explicit(atomic, mo_relaxed);
}

#define retain_obj(p)         (__retain_obj(&(p)->ref_count))
#define release_obj(p)        (__release_obj(&(p)->ref_count))
#define get_ref_count(p)      (__get_ref_count(&(p)->ref_count))
