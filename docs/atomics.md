
Use of atomics in Tilck
---------------------------

Being Tilck a non-SMP kernel, apparently, it should never need to use atomics.
Actually, that's not true. While it is true that there no need for any type
of *sequential consistency* in a non-SMP kernel, the need for simple atomicity
remains in order to the kernel to be portable. While on i386 and on x86_64
the load/store operations on pointer-size variables are inherently atomic (but
again, without sequential consistency guaratees), on other architectures,
typically RISC ones, that's simply not true. There, it might be even necessary
to use two instructions to store a 32-bit immediate value to a memory location.

Because of that, for variables that need to be accessed also from interrupt
context or for variables that are shared across multiple tasks (ref-counts),
C11 atomics are used with the memory-model parameter set to *relaxed*. With the
*relaxed* memory-model, on architectures like x86, the atomic load/store
operations will be translated into simple `MOV` instructions, as if there was no
involvment with atomic semantics at all while, on other architectures, special
instructions for atomics will be used.

Why we need simply atomicity for certain variables: examples
-------------------------------------------------------------

Think about a variable like `__disable_preempt`, used both in regular
code and in interrupt handlers. Now imagine that it takes two separate
instructions to store a value into it. What happens if an IRQs gets delivered
after the first instruction, but before the second one? Well get a *corrupt*
value for the variable and everything will get messed up.

The same example applies for reference count variables used in objects shared
across all the tasks in the system (like fs objects). What happens if, in the
middle of a non-atomic store to such reference count, the task gets preempted
by the scheduler and another task to made to run? We'll get a corrupted value.

Volatile vs Atomic
---------------------

Please note that the following implications are *not* true:

   - volatile -> atomic
   - atomic -> volatile

Therefore, it makes sense to have:

   - atomic variables which are *not* also volatile
   - volatile variables which are *not* also atomic
   - volatile atomic variables

An example: the `state` field in `struct task`. It needs to be *atomic* because
its value can be read and updated by interrupt handlers (see `wth.c`), but
it also needs to be *volatile* because it is read in loops (see `sys_waitpid`)
waiting for it to change. Theoretically, in case of consecutive atomic loads,
the compiler is *not* obliged to perform every time an actual read and it might
cache the value in a register, according to the C11 atomics model.

### Why `_Atomic` alone isn't enough: the C11 relaxed-atomics gap

The reason atomic-without-volatile is not a substitute for volatile-atomic
goes deeper than the example above. Per the C11/C17 memory model, two
adjacent `atomic_load_explicit(&x, memory_order_relaxed)` calls have *no*
sequenced-before or synchronizes-with relationship between them, so the
compiler is permitted to combine them into a single load. The same applies
to a load that no other observable behavior depends on — it can be hoisted
out of a loop. GCC and Clang in their current versions don't do this
aggressively for atomic operations, but that's an implementation choice,
not a language guarantee, and it can change between releases or under LTO.

`volatile` is the standard's mechanism for "every access is an observable
side effect; emit a real load/store every time." Tagging the field
`volatile` (Tilck's approach) and casting to volatile at each access site
(Linux's `READ_ONCE` / `WRITE_ONCE`) are two ways to spell the same
defensive guarantee. Either way, the compiler is forced to honor each
access.

### A note on the `sys_waitpid` example

The cited wait loop in `kernel/waitpid.c` is *currently* safe even without
volatile, but for a reason that doesn't generalize: the loop body calls
out-of-TU functions (`disable_preemption`, `prepare_to_wait_on`,
`enter_sleep_wait_state`, ...) and the compiler can't carry a cached
register value of `ti->state` across them — they're opaque and could
modify any memory. That's a *coincidental* protection, not a designed one.

Two ways that protection can disappear:

1. **Inlining changes.** A `static inline` annotation, LTO, or a refactor
   that brings the helpers into the same TU lets the compiler see they
   don't touch `ti->state` and hoist the load.

2. **Similar patterns elsewhere.** A future poll loop somewhere else in
   the kernel that does
   `while (atomic_load_explicit(&ti->state, mo_relaxed) != X) { ... }`
   without function-call barriers in the body has no such backstop and
   would silently break under `-O2`.

So `volatile` on `state` is the actual defense, doing real work even
where the current code happens to be safe for other reasons. The general
rule: **for any variable read by code that depends on observing a change
written elsewhere (an ISR, another task), prefer volatile-atomic over
plain atomic**, regardless of whether today's call graph happens to
serialize the accesses.
