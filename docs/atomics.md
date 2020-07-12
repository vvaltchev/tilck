
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

Think about a variable like `disable_preemption_count`, used both in regular
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

An example: the `state` field in `struct task`. It needs to be atomic because
its value can be read and updated by interrupt handlers (see `tasklet.c`), but
it also needs to be *volatile* because it is read in loops (see `sys_waitpid`)
waiting for it to change. Theoretically, in case of consecutive atomic loads,
the compiler is *not* obliged to perform every time an actual read and it might
cache the value in a register, according to the C11 atomic model. In practice,
with GCC this can happen only with relaxed atomics (the ones used in Tilck), at
best of my knowledge, but it is still good write C11-compliant code, instead of
relying on the compilers' behavior.
