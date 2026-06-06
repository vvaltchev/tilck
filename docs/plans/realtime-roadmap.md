# Tilck hard real-time roadmap

**Status:** forward-looking design. Not yet implemented. This document
captures the design intent, the decisions made so far, and a proposed
commit sequence for the work whenever it's ready to begin. When the
work *is* under way, a sibling `realtime.md` will track what's actually
built; today this single file plays both roles.

This is meant to be readable cold — by future-me or anyone else — and
to bootstrap a planning conversation without requiring the prior
discussion that produced it. Familiarity with the existing EEVDF
scheduler is assumed; see [docs/scheduler.md](scheduler.md). No
prior real-time systems background is assumed; concepts are
introduced inline.

## Vision

Tilck's longer-term direction is **hard real-time support**: an
admission-controlled scheduling class for tasks with strict timing
requirements, providing **analytical** (not empirical) bounds on the
latency between a task becoming runnable and it actually running, and
on the fraction of CPU it receives within a chosen period.

This is meaningfully different from Linux's `SCHED_FIFO` / `SCHED_RR`
/ `SCHED_DEADLINE`. Linux can offer *soft* real-time behavior — bounds
that hold empirically in most cases — but the kernel surface is too
large to analyze for hard bounds. Tilck's situation is the opposite:
the kernel is small enough to audit, single-CPU, no swapping, no
demand paging, no SMP cache coherency, a controlled driver set. The
*scheduling algorithm* is the easy part; the *audit* of every kernel
path an RT task can reach is the hard part, and Tilck's small footprint
makes that tractable.

### Why Tilck is a good fit

- **Small surface area.** Every `disable_interrupts()` and
  `disable_preemption()` region in `kernel/` can be audited for a
  measured worst-case duration. Linux can't make this claim about
  its own kernel.
- **No SMP today.** Removes a whole class of cross-CPU non-determinism
  (cache coherency, lock contention). When SMP arrives, the natural
  pattern is pinning RT tasks to dedicated CPUs.
- **Deterministic memory model.** No swap, no demand paging, no
  transparent huge pages, no overcommit. RT tasks pre-allocate at
  startup and never see a page fault on the hot path.
- **Predictable hardware.** Driver set is small enough to audit; the
  bootloader and init path already aim at <100 ms total for non-RT
  reasons.
- **Existing EEVDF substrate handles non-RT cleanly.** The fair-share
  scheduler we just built is the right home for "everything else"
  during RT operation — no need to rebuild it.

### What "hard" means here

Hard real-time means **timing guarantees backed by static analysis**,
not statistical testing. A task declaring "I need 200 µs of CPU every
1 ms" gets exactly that, every period, every time — or the system
refuses to admit the task at all. The compositional argument from
admission control plus CBS enforcement (described below) provides
that guarantee for the scheduling side; bounded-latency audits provide
it for the kernel side.

## The model: hierarchical CBS with slack reclamation

### Two buckets, not three

The CPU is partitioned into two classes:

- **RT class** — admission-controlled. Default cap **80 %** of CPU
  bandwidth (tunable via `KRN_RT_BANDWIDTH_PCT`, default `80`).
- **Non-RT class** — everything else. **20 %** floor. Within this
  class, **worker threads have strict priority over EEVDF tasks**,
  and EEVDF-task starvation by workers is acceptable by design (same
  semantics as today: `wth_get_runnable_thread()` runs before the
  EEVDF selector in `do_schedule()`).

We rejected a three-bucket split (RT / workers / EEVDF) because the
existing strict-priority relationship between workers and EEVDF tasks
already exists in `do_schedule()` for free. Sandwiching that
relationship inside a single non-RT bandwidth share preserves it with
no new dispatch logic.

### Reservations within the RT class

Each RT task declares:

- **C** = worst-case execution time per invocation (e.g. 200 µs).
- **T** = period (e.g. 1 ms).

From those: **utilization** `u_i = C_i / T_i` (in the example, 0.20).

Equivalent declarations the API should accept (all normalized to
`(C, T)` internally):

| Form                  | Example          | u   |
|-----------------------|------------------|-----|
| `(C, T)` directly     | `(200 µs, 1 ms)` | 0.20 |
| Utilization fraction  | `0.20`           | 0.20 |
| Frequency + WCET      | `1 kHz, 200 µs`  | 0.20 |
| MHz-of-3 GHz-style    | `600 MHz`        | 0.20 |

### Admission control

On the syscall that promotes a regular task to RT (sketch in
[syscall API](#syscall-api)), the scheduler checks:

```
sum(u_i) for all admitted RT tasks + u_new ≤ KRN_RT_BANDWIDTH_PCT / 100
```

If accepting `u_new` would push the sum above the RT bandwidth cap,
the syscall fails with `-EBUSY` (or `-EAGAIN`; bikeshed at
implementation time). Once admitted, the task is bound by **Constant
Bandwidth Server (CBS)** enforcement: a per-task budget `Q = C`
replenished at each period boundary, capped at `Q`, decremented as
the task runs. When `Q` hits zero, the task is throttled until the
next period begins. **EDF** (earliest-deadline-first) dispatches
within the RT class: the runnable RT task whose current period's
deadline is earliest gets the CPU. EDF on uniprocessor is *optimal*
under `sum(u_i) ≤ 1`, which the admission control already enforces.

### Dormancy when no RT tasks

A key design property: **while `sum(u_RT) == 0`, the partitioning
logic doesn't run at all**. `do_schedule()` is the existing 3-line
cascade (workers → EEVDF → idle). The CBS machinery engages only
when the first RT task is admitted, and disengages on the last RT
task exiting (or downgrading). This means:

- Non-RT workloads see cycle-identical behavior pre- and post-the-
  RT-subsystem landing, until something calls the admission syscall.
- Regression testing is trivial: existing test suites must pass
  unchanged.

### Slack reclamation

When the RT class doesn't fully use its budget — either because RT
tasks consume less than their declared WCET, or because some RT tasks
aren't currently runnable — the leftover bandwidth automatically
flows to the non-RT class. Concretely: if `do_schedule()` finds the
RT class has no runnable-and-in-budget task, it falls through to the
non-RT dispatch (workers → EEVDF) using the leftover time.

The RT class's 80 % is a **cap**, not a **reservation**. Non-RT gets
*at least* 20 %; it gets more whenever RT is partially idle.

If RT consumes nothing for a stretch, non-RT effectively has the full
CPU (with workers strict-priority over EEVDF). If RT consumes its
full 80 %, non-RT is hard-capped at 20 % for that period.

### Why slack flows downward only

We chose unidirectional slack (RT → non-RT, never non-RT → RT). The
reverse case ("non-RT idle, give the slack to RT") doesn't add value:
RT is admission-controlled to fit within its 80 %, so it doesn't
*need* more bandwidth. Giving it more would let it exceed its
declared WCET — a property we explicitly DON'T want (a CBS overrun
should remain visible as a throttle, not be silently absorbed by
slack).

### Period-based vs frame-based dispatch

Two variants of CBS scheduling exist; for Tilck the more flexible
**per-task-period** variant is the right default, with the simpler
**fixed-frame** variant available as a build option.

**Per-task-period (default):** each task has its own `T_i`. Deadlines
are computed per task per release. The scheduler is event-driven —
the next decision point is the earliest of "current task budget
exhausted" or "next task period boundary." Maximum flexibility; the
typical CBS implementation.

**Fixed-frame (build option):** a single global frame length
`T_frame` (e.g. 1 ms). All tasks have `T = T_frame` or `T =
T_frame * k`. Each frame has a pre-computed schedule. Maximally
predictable; this is the model used in ARINC 653 / IEC 61508 SIL-4
safety-critical systems (avionics, etc.). Less flexible but trivially
analyzable.

Default to per-task-period; consider fixed-frame as `KRN_RT_FRAME_MODE`
for deployments that need the deepest predictability and accept the
loss of flexibility.

## Architectural shape

### Dispatch order in do_schedule()

```
do_schedule():
   if (any RT task runnable AND in-budget):
      pick RT task by EDF
      run with budget-tracking until budget exhausted or preemption
   else:
      # non-RT class dispatch, same as today
      if (any worker runnable):
         pick worker by priority (wth_get_runnable_thread)
      else:
         pick EEVDF task (sched_do_select_runnable_task)
      else:
         idle
```

Per-tick accounting in `sched_account_ticks()` gains an RT branch:

```
if (curr is RT):
   decrement curr's CBS budget by elapsed time
   if budget exhausted:
      throttle curr (move out of runnable set until next period)
      sched_set_need_resched()
```

### Syscall API

Sketch — bikeshed names at implementation time:

```c
/* Promote curr to RT class with the given reservation. Admission-
 * controlled: returns 0 on accept, -EBUSY if the request would
 * overcommit the RT bandwidth cap, -EINVAL on garbage input. */
int sys_sched_make_rt(struct sched_rt_attr *attr);

/* Drop curr back to non-RT (EEVDF). Releases the reservation. */
int sys_sched_clear_rt(void);

struct sched_rt_attr {
    u64 runtime_ns;   /* C in nanoseconds (or microseconds?) */
    u64 period_ns;    /* T in nanoseconds */
    u32 flags;        /* future expansion */
};
```

Open questions on the API:

- **Granularity unit.** Nanoseconds, microseconds, or something
  HPET-tick-derived? Nanoseconds match Linux's SCHED_DEADLINE and
  feel future-proof; microseconds match Tilck's existing time-unit
  conventions in `kernel/timer.c` and avoid u64 overflow worries.
- **Self-only or arbitrary-task?** Linux SCHED_DEADLINE accepts a
  pid; Tilck could restrict to self-only (the calling task makes
  itself RT) to dodge a class of privilege issues. Probably the
  right starter default.
- **Multi-reservation declarations.** Some workloads want
  `(C, T, D)` with deadline `D ≤ T` to express "I need this done
  earlier than my period boundary." Defer; start with `D = T`
  (the implicit-deadline case) and add explicit deadlines later
  if needed.

### Where RT data lives

`struct task` extensions (`include/tilck/kernel/sched.h`):

```c
enum sched_class {
    SCHED_CLASS_NONRT = 0,   /* default — EEVDF or worker */
    SCHED_CLASS_RT    = 1,
};

struct sched_rt {
    u64 budget_ns;        /* remaining C in current period (Q) */
    u64 period_ns;        /* T */
    u64 wcet_ns;          /* declared C */
    u64 deadline_ns;      /* current period's absolute deadline */
    u32 utilization_x1k;  /* u × 1000, for admission accounting */
};

struct task {
    /* ...existing fields... */
    enum sched_class sclass;
    struct sched_rt rt;
};
```

Non-RT tasks don't touch `rt`; the field sits unused. Pure bookkeeping
addition.

### Where the RT ready set lives

Separate from the EEVDF AVL. The RT set is small (admission control
keeps `sum(u_i) ≤ 0.80`, so practical task counts are small — maybe
dozens). A simple ordered structure suffices:

- Small AVL keyed by `(deadline_ns, tid)` for in-order EDF dispatch.
- Or a fixed-size sorted list if we want to bound allocation at boot.
- Or, with the fixed-frame variant, a pre-computed schedule table.

The bintree.h infrastructure handles the AVL case cleanly with the
same idiom used for `runnable_tree_root`.

## Prerequisites

### 1. High-resolution timer

The PIT at 250 Hz (4 ms granularity) is too coarse for RT. Periods
in the 100 µs to 10 ms range are typical, and CBS budget enforcement
needs to fire at the next "earliest of (budget exhaustion, period
boundary)" event — which can be hundreds of microseconds away.

Options:

- **APIC one-shot timer.** Available on every modern x86. Sub-µs
  programming. Local to the CPU (relevant once SMP arrives).
- **HPET.** External, slightly higher access cost but architecturally
  simpler. Available on most x86 platforms since ~2005.

Either works. APIC is more universal and probably the right primary
target; HPET as a fallback for boards with broken APIC timers.

### 2. x86-64 as the primary target

i386 *could* host this work, but the audit effort is substantial and
hardware support is more uniform on x86-64. The decision is to
sequence: x86-64 becomes the primary build target first, then RT
support is added on top. i386 may follow or may stay non-RT
indefinitely depending on how much of the audit transfers.

### 3. Bounded-latency audit of the kernel

For every kernel path an RT task can reach, a measured worst-case
duration. Specifically:

- Every `disable_interrupts()` region (today these are short — tens
  to hundreds of nanoseconds — but no formal measurement exists).
- Every `disable_preemption()` region.
- Every IRQ handler from acknowledge to bottom-half-dispatch (the
  top-half work; the bottom-half runs in the worker class and is
  budget-tracked).
- Every syscall path used by RT tasks (more on this below).

The audit produces a **single number**: the maximum non-preemptible
interval Tilck guarantees. That number is the irreducible latency
floor for the RT class; everything below it is the RT task's CPU
share.

### 4. Restricted syscall surface for RT tasks

Easier to forbid most syscalls in RT tasks than to bound them all.
Common pattern: RT tasks get a tiny RT-safe subset (`clock_nanosleep`,
specific read/write FDs, signal-style notifications) and any other
syscall returns `-ENOSYS` or `-EPERM` while in RT class. Decide the
exact subset when the audit is in progress — it's easier to specify
what's allowed once you know what's expensive.

### 5. Pre-allocated memory and pinned pages

RT tasks must pre-allocate their working set at admission time and
the kernel must verify they never fault on the hot path. This may
need a `mlockall`-equivalent syscall (or be automatic on
`sys_sched_make_rt`).

## Step-by-step plan

Each step is intended to be a single, self-contained, bisectable
commit (project rule), with the property that the scheduler doesn't
get worse than current master between steps. Sequence mirrors the
shape used for the EEVDF roadmap (E1-E7); call these R1-R8 if/when
implemented.

### R1. High-resolution timer infrastructure

Add APIC one-shot timer support (with HPET as a fallback). Plumb a
`u64 sched_get_time_ns()` query and a `sched_arm_timer_at(deadline_ns)`
primitive. No scheduler changes yet; just the infrastructure.

The existing PIT remains for the tick-based counters (`ticks.total`
etc.) and for the EEVDF per-tick increment. The new timer is the
*event timer* used by CBS.

### R2. struct task RT extension and class enum

Add `sched_class sclass` and `struct sched_rt rt` to `struct task`.
Initialize to `SCHED_CLASS_NONRT` at fork. No dispatch changes; pure
bookkeeping. Memory cost: a few extra `u64` fields per task —
negligible.

### R3. Stub syscall API

Add `sys_sched_make_rt` / `sys_sched_clear_rt` returning `-ENOSYS`
for now. Establish the userspace ABI early so callers can be staged.

### R4. RT ready-set data structure + EDF dispatch

Add a separate AVL (or sorted list) for the RT class, keyed by
`(deadline_ns, tid)`. Add an RT-pick step in `do_schedule()` *before*
the worker/EEVDF cascade:

```
selected = pick_rt_task();   /* nullable */
if (!selected) selected = wth_get_runnable_thread();
if (!selected) selected = sched_do_select_runnable_task(...);
```

`pick_rt_task()` returns nullable; while no RT tasks exist, it's a
single null check and a return. Dormancy verified.

### R5. CBS budget enforcement

Per-tick (or per-event, see R8) decrement of `curr->rt.budget_ns` if
`curr->sclass == SCHED_CLASS_RT`. On budget exhaustion: throttle
(remove from RT ready set) and `sched_set_need_resched()`. On period
boundary: replenish budget to `wcet_ns`, recompute next deadline,
re-insert into RT ready set if previously throttled.

### R6. Admission control

Wire up `sys_sched_make_rt`. Maintain a `STATIC atomic_u32_t
sum_utilization_x1k`; the syscall is:

```c
if (sum_utilization_x1k + new_u_x1k > KRN_RT_BANDWIDTH_PCT * 10)
    return -EBUSY;

sum_utilization_x1k += new_u_x1k;
curr->sclass = SCHED_CLASS_RT;
curr->rt = ...;
return 0;
```

(With appropriate locking. The sum is in u_x1000 units to keep the
math integer.)

### R7. Bounded-latency audit pass

Measure (don't just inspect) every `disable_interrupts` /
`disable_preemption` region under realistic workloads. Use the
high-resolution timer from R1 to instrument entry and exit. Produce
a table of (location, measured max). Document the irreducible latency
floor in this file. Identify regions whose max exceeds the floor
target and either shorten them or document why they're acceptable.

This is the longest-lead-time step and may itself decompose into
multiple commits.

### R8. RT-safe syscall subset

Decide which syscalls RT tasks can call without breaking bounds.
Gate the others with an `EPERM` (or `ENOSYS`) check on
`curr->sclass == SCHED_CLASS_RT`. Document the subset.

### Optional later steps

- **Fixed-frame mode** (`KRN_RT_FRAME_MODE`): replace event-driven
  dispatch with a pre-computed frame schedule. Higher predictability,
  lower flexibility. Pick for deepest hard-RT deployments only.
- **Interrupt budgeting**: account interrupt-handler time against
  the RT class's budget rather than letting it preempt freely. Makes
  IRQ latency a first-class scheduling resource.
- **Memory locking on admission**: walk the task's address space at
  admission time, pre-fault every page, refuse if the working set
  exceeds a declared budget.
- **Cache partitioning** (when SMP arrives): Intel CAT or
  equivalent. RT tasks declare cache reservations as part of their
  resource set.
- **Refined eligibility predicate** in EEVDF: see
  [docs/scheduler.md](scheduler.md#refined-eligibility-predicate).
  Becomes relevant once RT slack reclamation pushes variable amounts
  of CPU into the non-RT class and EEVDF needs to fair-share more
  precisely than today.

## Glossary

- **Admission control** — the scheduler accepts or rejects an RT task
  declaration based on whether the resulting task set fits within the
  RT class's bandwidth cap. Once accepted, the schedulability is
  *proven*, not just observed.
- **C** — worst-case execution time of one task invocation
  (microseconds or nanoseconds).
- **CBS (Constant Bandwidth Server)** — Abeni & Buttazzo 1998.
  Per-task budget `Q` replenished to `C` at each period boundary.
  Enforces `u = C / T` utilization for the task. Overruns are
  throttled, not silently absorbed.
- **Deadline** — in RT terms, the absolute time by which a task's
  current period invocation must complete. The EDF sort key.
- **EDF (Earliest Deadline First)** — dynamic-priority scheduling
  picking the runnable task with the earliest deadline. Optimal on
  uniprocessor under `sum(u_i) ≤ 1`.
- **Hard real-time** — timing guarantees backed by analytical proof
  rather than empirical testing. The system either meets the
  deadline always, or refuses the task at admission time.
- **Hierarchical CBS** — nested CBS where a class has a global budget
  and tasks within the class have sub-budgets. Tilck's model is
  two-level: RT class budget (80 % cap), non-RT class budget (20 %
  floor).
- **Period (T)** — the inter-arrival time of an RT task's
  invocations.
- **Slack reclamation** — when one class doesn't consume its full
  budget, the leftover flows to another class. Tilck's model is
  unidirectional (RT → non-RT).
- **Soft real-time** — timing requirements that *should* be met
  most of the time but for which occasional misses are tolerable.
  Linux's territory.
- **Utilization (u)** — `C / T`. Fraction of CPU one task needs.
- **WCET (worst-case execution time)** — the longest possible
  execution time of one task invocation, considered across all
  inputs. Hard to compute precisely; often estimated with safety
  margin or measured with conservative test inputs.

## References

The reservation-based + CBS + EDF model has substantial literature:

- Abeni & Buttazzo, *"Integrating multimedia applications in
  hard real-time systems"* (1998) — the original CBS paper.
- Liu & Layland, *"Scheduling algorithms for multiprogramming in a
  hard real-time environment"* (1973) — EDF + RMS schedulability
  bounds.
- Buttazzo, *Hard Real-Time Computing Systems* (textbook) — the
  standard reference.

Linux's `SCHED_DEADLINE` (kernel/sched/deadline.c) is the closest
mainline implementation of this model and is useful prior art, even
though we explicitly aim for harder guarantees than Linux provides.

ARINC 653 (avionics) is the canonical model for fixed-frame
time-partitioning if/when `KRN_RT_FRAME_MODE` is implemented.
