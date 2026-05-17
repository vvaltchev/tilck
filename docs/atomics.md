Use of atomics in Tilck
=======================

Tilck is a non-SMP kernel; *sequential consistency* across CPUs is
never needed. **Simple atomicity** still is, because:

  - Some variables are accessed from both regular code and IRQ
    handlers (preemption nesting counters, scheduler flags, the
    bogoMIPS calibration counter, ref-counts, …). A store that
    isn't a single indivisible operation can be interrupted
    half-written, leaving the value corrupt.

  - Non-x86 architectures (notably riscv64) sometimes need
    multiple instructions to store an integer to memory. Even
    when the variable is naturally aligned, that's still a
    multi-step write whose intermediate state would be visible
    to an interrupt.

The atomic layer used to be a thin wrapper over C11 `_Atomic` and
`atomic_*_explicit`. After a series of pain points accumulated
(see "Why not C11 `_Atomic` directly?" below) we replaced it with
a small Tilck-specific layer that delivers the same guarantees
without those costs.

The new API
-----------

Defined in `include/tilck/common/atomics.h`.

### Type set

```
atomic_s8_t   atomic_u8_t
atomic_s16_t  atomic_u16_t
atomic_s32_t  atomic_u32_t
atomic_s64_t  atomic_u64_t
atomic_int_t
atomic_bool_t
atomic_ptr_t     /* untyped void *, used by wait_obj.__ptr */
```

Each type is a struct wrapping a single plain field `v`. The
wrapper exists so the field type itself enforces "no accidental
non-atomic access": `int x; x = 5;` works at any call site, but
`atomic_int_t x; x = 5;` is a compile error (struct assignment
without an initializer of the same struct type). Writes must go
through `atomic_store_int(&x, 5)`.

64-bit types carry `ALIGNED_AT(8)` so `lock cmpxchg8b` on i386 is
guaranteed atomic against cache-line crossings. On x86_64 /
riscv64 the attribute is a no-op (natural alignment).

### Sugar

```c
#define atomic(x) atomic_##x##_t
```

So `atomic(int) foo;` is `atomic_int_t foo;`, `atomic(u32) bar;`
is `atomic_u32_t bar;`, etc. Lowercase deliberately — as a
function-like macro it only fires when followed by `(`, so it
doesn't collide with C++'s `std::atomic<T>` (no `(`).

### Operations

For every atomic type:

```c
TYPE atomic_load_<type>      (atomic_<type>_t *p);
void atomic_store_<type>     (atomic_<type>_t *p, TYPE val);
TYPE atomic_exchange_<type>  (atomic_<type>_t *p, TYPE val);
bool atomic_cas_weak_<type>  (atomic_<type>_t *p, TYPE *expected, TYPE desired);
bool atomic_cas_strong_<type>(atomic_<type>_t *p, TYPE *expected, TYPE desired);
```

Integer types additionally have:

```c
TYPE atomic_fetch_add_<type> (atomic_<type>_t *p, TYPE val);
TYPE atomic_fetch_sub_<type> (atomic_<type>_t *p, TYPE val);
```

All wrappers are `ALWAYS_INLINE`. Memory ordering is implicit
`mo_relaxed` and not parameterized — every in-tree caller is fine
with relaxed; if some future site needs a stronger order, we'll
add a separately-named wrapper (e.g. `atomic_load_acquire_int`)
rather than parameterize.

Implementation
--------------

Inside each wrapper, the field pointer is cast to
`(T volatile *)&p->v`. The volatile half is what guarantees the
no-fold-no-hoist behavior (Linux's `READ_ONCE`/`WRITE_ONCE`
pattern); the access is then handed to a compiler builtin:

  - **Most cases**: `__atomic_load_n` / `__atomic_store_n` /
    `__atomic_fetch_add` / `__atomic_fetch_sub` /
    `__atomic_exchange_n` / `__atomic_compare_exchange_n`. These
    are GCC/clang intrinsics, header-free.

  - **i386 u64 / s64**: `__sync_val_compare_and_swap`,
    `__sync_fetch_and_add`, etc. The `__atomic_*` builtins on
    i386 64-bit operands route through libatomic, whose
    implementation uses `FILDLL` / `FISTPLL` (FPU) — forbidden
    in kernel context. `__sync_*` inlines directly to
    `lock cmpxchg8b` and stays clear of libatomic. The atomic-
    load shape uses the "CAS(0,0)" trick: a compare-exchange
    with expected=desired=0 reads atomically and only writes 0
    when *p is already 0 (a no-op store). Same trick Linux's
    `atomic64_read` uses on 32-bit x86.

The result is that the kernel binary contains no FPU instructions
in atomic paths and no libatomic dependency. Verify with:

```
objdump -d build/tilck | grep -E 'fildll|fistpll|__atomic_load_8|__atomic_store_8|__atomic_fetch_add_8'
```

(should be empty).

Why we don't include `<stdatomic.h>`
------------------------------------

Everything above lives on top of compiler intrinsics. The C11
`<stdatomic.h>` (and C++ `<atomic>`) would add typedefs
(`atomic_int`, `atomic_bool`, …) that collide with our short
names, plus thin wrap macros over the same `__atomic_*` builtins,
plus the `memory_order_*` enum which equals the
compiler-predefined `__ATOMIC_*` constants. None of those add
value over the direct intrinsics, so we skip them. The header has
zero system-header dependency on the atomic side.

Why not C11 `_Atomic` directly?
-------------------------------

The previous design used `_Atomic(T)` as the storage type
(`ATOMIC(int) foo`, `volatile ATOMIC(enum) state`, …). We moved
away from it after hitting five separate pain points:

1. **i386 u64 routes through libatomic, which uses FPU.** GCC
   emits libcalls to `__atomic_load_8` / `__atomic_store_8` for
   any `_Atomic(u64)` access on i386 (regardless of `-O`,
   regardless of `-march=i686`). The toolchain's libatomic
   implementation of those uses `FILDLL`/`FISTPLL` — FPU
   instructions, which the kernel forbids outside `fpu_context`.
   First access panics.

2. **`_Atomic(u64)` bumps containing-struct alignment.** On
   i386, plain `u64` has 4-byte alignment but `_Atomic(u64)`
   has 8-byte. Any struct containing it inherits the bump; any
   cast from a 4-aligned pointer to a struct-now-8-aligned trips
   `-Wcast-align` under clang.

3. **GCC 11.0→11.1 ABI change.** `_Atomic(long long)` alignment
   changed in 11.1; GCC emits a per-TU `-Wpsabi` note for every
   consumer of every header that declares the field.

4. **`_Atomic` doesn't exist in C++.** That forced the
   pre-existing atomics.h to maintain three parallel paths (C,
   C++ with `<atomic>`, C++ under `KERNEL_FORCE_TC_ISYSTEM` with
   `fake_atomics.h`).

5. **`volatile` was load-bearing.** Because C11 lets the
   compiler combine or hoist `mo_relaxed` loads, atomic fields
   that needed every access to be a real load/store (e.g.
   `task.state`, `__bogo_loops`) were declared
   `volatile ATOMIC(...)` and the rationale was documented at
   each site.

By using plain `T` storage and casting to `_Atomic(T)`-equivalent
qualifiers on the fly inside the wrapper — actually, by using
`__atomic_*` builtins on `(T volatile *)` and skipping the
`_Atomic` keyword entirely — all five pain points go away. The
i386 u64 case escapes to `__sync_*` and stays clear of
libatomic; the field-alignment surprises disappear; there's no
psabi note; the wrapper compiles uniformly in C and C++; and
every wrapper call is volatile-cast, so call sites never need to
write `volatile` themselves.

### Side benefit: `fake_atomics.h` is gone

The legacy `include/tilck/common/fake_atomics.h` was needed only
because C++ couldn't include `<atomic>` under the
`KERNEL_FORCE_TC_ISYSTEM` static-analysis build. With the new
layer there's no `<atomic>` reference at all, so the
static-analysis build compiles without any fallback, and the
file has been deleted.

Examples
--------

A scheduler counter that an IRQ increments while regular code
reads:

```c
static atomic_int_t runnable_tasks_count;

void on_runnable(struct task *t)
{
   atomic_fetch_add_int(&runnable_tasks_count, 1);
}

int get_runnable_tasks_count(void)
{
   return atomic_load_int(&runnable_tasks_count);
}
```

A bool flag with CAS-based handoff:

```c
static atomic_bool_t lock_in_use;

bool try_lock(void)
{
   bool expected = false;
   return atomic_cas_strong_bool(&lock_in_use, &expected, true);
}

void unlock(void)
{
   atomic_store_bool(&lock_in_use, false);
}
```

A pointer slot accessed atomically:

```c
struct wait_obj {
   atomic_ptr_t __ptr;
   ...
};

void *p = atomic_load_ptr(&wo->__ptr);  /* cast at the call site
                                          to the concrete type */
```

A bitfield-packed `u32` updated via CAS loop:

```c
struct ringbuf_stat {
   union {
      struct { u32 read_pos:14; u32 write_pos:14; u32 full:1; ... };
      atomic_u32_t raw;
      u32 __raw;
   };
};

do {
   cs = stat;                 /* read both views */
   ns = stat;
   /* compute updated ns ... */
} while (!atomic_cas_weak_u32(&stat.raw, &cs.__raw, ns.__raw));
```
