/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck_gen_headers/config_debug.h>

#define __TILCK_ATOMICS__

/*
 * Tilck atomic operations layer.
 *
 * Note: we deliberately do NOT #include <stdatomic.h> nor C++'s
 * <atomic>. Everything below is built on top of the compiler-
 * provided `__atomic_*` and `__sync_*` builtins (GCC, clang),
 * which require no header. What stdatomic.h / <atomic> would add
 * on top of those primitives is:
 *
 *   1. Typedefs like `atomic_int`, `atomic_bool`, `atomic_uint`,
 *      ... -- these would collide with our `atomic_<type>_t`
 *      typedef family below. <atomic> additionally exposes them
 *      as `std::atomic_int` etc., which gtest .cpp files pull
 *      into the global namespace with `using namespace std;`,
 *      reintroducing the same collision.
 *
 *   2. The lowercase `atomic_*_explicit` macros, each at most a
 *      few-line `({ ... })` wrap of an `__atomic_*` builtin.
 *      The new `atomic_<op>_<type>` wrappers below replace them
 *      entirely; we no longer have any caller for the legacy
 *      surface.
 *
 *   3. The `memory_order_*` enum, equal to the compiler-
 *      predefined `__ATOMIC_*` integer constants. The new
 *      wrappers hard-code `__ATOMIC_RELAXED` and don't expose
 *      memory ordering at the call site.
 *
 *   4. The `ATOMIC_*_LOCK_FREE` constants. The compiler exposes
 *      the same values as `__GCC_ATOMIC_*_LOCK_FREE` (clang
 *      defines those identifiers too for GCC compatibility).
 *
 * Side benefit: the atomic layer is self-contained, has zero
 * system-header dependency, and behaves identically in C and C++
 * source.
 */

/* ============================================================
 *  Lock-free static checks via compiler-predefined macros.
 *  riscv64 toolchains we ship may not have hardware byte / half
 *  atomics; their `>= 1` ("sometimes lock-free") is acceptable.
 *  i386 / x86_64 must be `== 2` ("always lock-free").
 * ============================================================
 */

#ifdef __riscv64
   STATIC_ASSERT(__GCC_ATOMIC_BOOL_LOCK_FREE >= 1);
   STATIC_ASSERT(__GCC_ATOMIC_CHAR_LOCK_FREE >= 1);
   STATIC_ASSERT(__GCC_ATOMIC_SHORT_LOCK_FREE >= 1);
#else
   STATIC_ASSERT(__GCC_ATOMIC_BOOL_LOCK_FREE == 2);
   STATIC_ASSERT(__GCC_ATOMIC_CHAR_LOCK_FREE == 2);
   STATIC_ASSERT(__GCC_ATOMIC_SHORT_LOCK_FREE == 2);
#endif

STATIC_ASSERT(__GCC_ATOMIC_INT_LOCK_FREE == 2);
STATIC_ASSERT(__GCC_ATOMIC_LONG_LOCK_FREE == 2);
STATIC_ASSERT(__GCC_ATOMIC_POINTER_LOCK_FREE == 2);


/* ============================================================
 *  Type definitions
 * ============================================================
 *
 *  Struct-wrapped plain-typed storage. The struct wrapper gives
 *  back the type-checking benefit we'd otherwise lose by dropping
 *  `_Atomic`: a plain `int x; ... x = 5;` is a compile error
 *  against an `atomic_int_t` field (struct assignment, wrong
 *  type). Writes must go through `atomic_store_int(&x, 5)`.
 *
 *  The 64-bit struct forces 8-byte alignment via `ALIGNED_AT(8)`
 *  so that `lock cmpxchg8b` on i386 (and a single MOV on
 *  x86_64/riscv64) is naturally aligned. On x86_64/riscv64 the
 *  attribute is a no-op (native alignment is already 8).
 */

typedef struct { s8    v;               } atomic_s8_t;
typedef struct { u8    v;               } atomic_u8_t;
typedef struct { s16   v;               } atomic_s16_t;
typedef struct { u16   v;               } atomic_u16_t;
typedef struct { s32   v;               } atomic_s32_t;
typedef struct { u32   v;               } atomic_u32_t;
typedef struct { s64   v ALIGNED_AT(8); } atomic_s64_t;
typedef struct { u64   v ALIGNED_AT(8); } atomic_u64_t;
typedef struct { int   v;               } atomic_int_t;
typedef struct { bool  v;               } atomic_bool_t;
typedef struct { void *v;               } atomic_ptr_t;

/*
 * Generic sugar: `atomic(x)` expands to `atomic_<x>_t`. Lowercase
 * intentionally -- the function-like form (`atomic(int) foo;`) is
 * safe in C++ even with `using namespace std;` because the macro
 * only fires when followed by `(`, while `std::atomic<int>` uses
 * `<`. See docs/atomics.md for the rationale.
 */
#define atomic(x) atomic_##x##_t


/* ============================================================
 *  Operation generators
 * ============================================================
 *
 *  Each wrapper:
 *
 *   - takes an `atomic_<type>_t *` (struct-wrapped plain field);
 *   - casts to `(T volatile *)&p->v` so every access is a real
 *     load/store (Linux's READ_ONCE / WRITE_ONCE pattern);
 *   - dispatches to a compiler builtin: `__atomic_*` for native
 *     atomic ops, `__sync_*` for the i386 u64/s64 special case
 *     (C11's `__atomic_*` on i386 u64 routes through libatomic,
 *     whose implementation uses FPU -- forbidden in kernel
 *     context. `__sync_*` inlines directly to `lock cmpxchg8b`);
 *   - hard-codes `__ATOMIC_RELAXED`. If a future caller needs a
 *     stronger order, we add a separately-named wrapper rather
 *     than parameterize.
 *
 *  Three flavors:
 *
 *   _DEFINE_ATOMIC_INT_OPS         -- integers (<=32-bit on all
 *                                     archs, 64-bit on non-i386).
 *                                     7 ops via __atomic_*.
 *
 *   _DEFINE_ATOMIC_NONINT_OPS      -- atomic_bool / atomic_ptr.
 *                                     5 ops (no fetch_add/sub).
 *
 *   _DEFINE_ATOMIC_INT_OPS_I386_64 -- i386 u64/s64. 7 ops via
 *                                     __sync_*.
 *
 *  `(T volatile *)&p->v` is the volatile-cast pattern; it works
 *  uniformly for scalar and pointer types (for `T = void *` it
 *  yields `(void * volatile *)`, qualifying the pointer being
 *  read, not its target).
 */

#define _DEFINE_ATOMIC_INT_OPS(name, T)                                  \
                                                                         \
   static ALWAYS_INLINE T                                                \
   atomic_load_##name(atomic_##name##_t *p)                              \
   {                                                                     \
      return __atomic_load_n(                                            \
         (T volatile *)&p->v,                                            \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE void                                             \
   atomic_store_##name(atomic_##name##_t *p, T val)                      \
   {                                                                     \
      __atomic_store_n(                                                  \
         (T volatile *)&p->v,                                            \
         val,                                                            \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE T                                                \
   atomic_fetch_add_##name(atomic_##name##_t *p, T val)                  \
   {                                                                     \
      return __atomic_fetch_add(                                         \
         (T volatile *)&p->v,                                            \
         val,                                                            \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE T                                                \
   atomic_fetch_sub_##name(atomic_##name##_t *p, T val)                  \
   {                                                                     \
      return __atomic_fetch_sub(                                         \
         (T volatile *)&p->v,                                            \
         val,                                                            \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE T                                                \
   atomic_exchange_##name(atomic_##name##_t *p, T val)                   \
   {                                                                     \
      return __atomic_exchange_n(                                        \
         (T volatile *)&p->v,                                            \
         val,                                                            \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE bool                                             \
   atomic_cas_weak_##name(atomic_##name##_t *p, T *exp, T des)           \
   {                                                                     \
      return __atomic_compare_exchange_n(                                \
         (T volatile *)&p->v,                                            \
         exp,                                                            \
         des,                                                            \
         1,                                                              \
         __ATOMIC_RELAXED,                                               \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE bool                                             \
   atomic_cas_strong_##name(atomic_##name##_t *p, T *exp, T des)         \
   {                                                                     \
      return __atomic_compare_exchange_n(                                \
         (T volatile *)&p->v,                                            \
         exp,                                                            \
         des,                                                            \
         0,                                                              \
         __ATOMIC_RELAXED,                                               \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }

#define _DEFINE_ATOMIC_NONINT_OPS(name, T)                               \
                                                                         \
   static ALWAYS_INLINE T                                                \
   atomic_load_##name(atomic_##name##_t *p)                              \
   {                                                                     \
      return __atomic_load_n(                                            \
         (T volatile *)&p->v,                                            \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE void                                             \
   atomic_store_##name(atomic_##name##_t *p, T val)                      \
   {                                                                     \
      __atomic_store_n(                                                  \
         (T volatile *)&p->v,                                            \
         val,                                                            \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE T                                                \
   atomic_exchange_##name(atomic_##name##_t *p, T val)                   \
   {                                                                     \
      return __atomic_exchange_n(                                        \
         (T volatile *)&p->v,                                            \
         val,                                                            \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE bool                                             \
   atomic_cas_weak_##name(atomic_##name##_t *p, T *exp, T des)           \
   {                                                                     \
      return __atomic_compare_exchange_n(                                \
         (T volatile *)&p->v,                                            \
         exp,                                                            \
         des,                                                            \
         1,                                                              \
         __ATOMIC_RELAXED,                                               \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE bool                                             \
   atomic_cas_strong_##name(atomic_##name##_t *p, T *exp, T des)         \
   {                                                                     \
      return __atomic_compare_exchange_n(                                \
         (T volatile *)&p->v,                                            \
         exp,                                                            \
         des,                                                            \
         0,                                                              \
         __ATOMIC_RELAXED,                                               \
         __ATOMIC_RELAXED                                                \
      );                                                                 \
   }

#define _DEFINE_ATOMIC_INT_OPS_I386_64(name, T)                          \
                                                                         \
   static ALWAYS_INLINE T                                                \
   atomic_load_##name(atomic_##name##_t *p)                              \
   {                                                                     \
      /*                                                                 \
       * CAS(0,0): reads atomically via `lock cmpxchg8b`;                \
       * writes 0 only when *p is already 0 (no-op store).               \
       * Same trick Linux's atomic64_read uses on 32-bit x86.            \
       */                                                                \
      return __sync_val_compare_and_swap(&p->v, (T)0, (T)0);             \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE void                                             \
   atomic_store_##name(atomic_##name##_t *p, T val)                      \
   {                                                                     \
      T old;                                                             \
      do {                                                               \
         old = *(T volatile *)&p->v;                                     \
      } while (!__sync_bool_compare_and_swap(&p->v, old, val));          \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE T                                                \
   atomic_fetch_add_##name(atomic_##name##_t *p, T val)                  \
   {                                                                     \
      return __sync_fetch_and_add(&p->v, val);                           \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE T                                                \
   atomic_fetch_sub_##name(atomic_##name##_t *p, T val)                  \
   {                                                                     \
      return __sync_fetch_and_sub(&p->v, val);                           \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE T                                                \
   atomic_exchange_##name(atomic_##name##_t *p, T val)                   \
   {                                                                     \
      /* CAS loop -- full barrier, matches the rest of the API. */     \
      T old;                                                             \
      do {                                                               \
         old = *(T volatile *)&p->v;                                     \
      } while (!__sync_bool_compare_and_swap(&p->v, old, val));          \
      return old;                                                        \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE bool                                             \
   atomic_cas_weak_##name(atomic_##name##_t *p, T *exp, T des)           \
   {                                                                     \
      T old = __sync_val_compare_and_swap(&p->v, *exp, des);             \
      if (old == *exp)                                                   \
         return true;                                                    \
      *exp = old;                                                        \
      return false;                                                      \
   }                                                                     \
                                                                         \
   static ALWAYS_INLINE bool                                             \
   atomic_cas_strong_##name(atomic_##name##_t *p, T *exp, T des)         \
   {                                                                     \
      T old = __sync_val_compare_and_swap(&p->v, *exp, des);             \
      if (old == *exp)                                                   \
         return true;                                                    \
      *exp = old;                                                        \
      return false;                                                      \
   }

#define _ATOMIC_INT_TYPES_LE32(X)                                        \
   X(s8,   s8)                                                           \
   X(u8,   u8)                                                           \
   X(s16,  s16)                                                          \
   X(u16,  u16)                                                          \
   X(s32,  s32)                                                          \
   X(u32,  u32)                                                          \
   X(int,  int)

#define _ATOMIC_INT_TYPES_64(X)                                          \
   X(s64,  s64)                                                          \
   X(u64,  u64)

#define _ATOMIC_NONINT_TYPES(X)                                          \
   X(bool, bool)                                                         \
   X(ptr,  void *)

_ATOMIC_INT_TYPES_LE32(_DEFINE_ATOMIC_INT_OPS)
_ATOMIC_NONINT_TYPES(_DEFINE_ATOMIC_NONINT_OPS)

#if defined(__i386__)
   _ATOMIC_INT_TYPES_64(_DEFINE_ATOMIC_INT_OPS_I386_64)
#else
   _ATOMIC_INT_TYPES_64(_DEFINE_ATOMIC_INT_OPS)
#endif


/* ============================================================
 *  Basic reference counting
 * ============================================================
 *
 *  Storage is `atomic_int_t ref_count` so the field type itself
 *  enforces atomic-only access (no accidental `p->ref_count++`).
 *  Initialization at struct creation goes through `.v`:
 *
 *      pi->ref_count = (atomic_int_t){ .v = 1 };
 */

#define REF_COUNTED_OBJECT    atomic_int_t ref_count

#if !SLOW_DEBUG_REF_COUNT

/* Return the new value */
static ALWAYS_INLINE int __retain_obj(atomic_int_t *ref_count)
{
   return atomic_fetch_add_int(ref_count, 1) + 1;
}

/* Return the new value */
static ALWAYS_INLINE int __release_obj(atomic_int_t *ref_count)
{
   int old = atomic_fetch_sub_int(ref_count, 1);
   ASSERT(old > 0);
   return old - 1;
}

#else

int __retain_obj(atomic_int_t *ref_count);
int __release_obj(atomic_int_t *ref_count);

#endif

static ALWAYS_INLINE int __get_ref_count(atomic_int_t *ref_count)
{
   return atomic_load_int(ref_count);
}

#define retain_obj(p)         (__retain_obj(&(p)->ref_count))
#define release_obj(p)        (__release_obj(&(p)->ref_count))
#define get_ref_count(p)      (__get_ref_count(&(p)->ref_count))
