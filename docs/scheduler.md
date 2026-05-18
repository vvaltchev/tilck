# Tilck scheduler

Companion to `kernel/sched.c`. This file captures the *current* scheduler
behavior — the result of the 7-step CFS-style roadmap landed across
commits `b705eb29` ... `b9cc13a8` (and the prerequisite atomics work
that preceded them) — and outlines the planned next move to **EEVDF**
(Earliest Eligible Virtual Deadline First), building on the same
substrate. The realtime aspiration mentioned at the end is forward-
looking only; this document does not design it.

The file is meant to be readable by someone coming back to scheduler
code after weeks or months of detour. Skim the "Current state" section
first; the "EEVDF roadmap" section assumes that context.

## Current state (post-CFS roadmap)

### What the scheduler is

Tilck has a single-CPU, preemptive, fair-share scheduler. It picks
the runnable task with the lowest accumulated **vruntime** (virtual
runtime), with a per-task **dynamic time slice** capped by a target
scheduling latency and floored at a minimum granularity.

It is *not* yet EEVDF: there is no per-task deadline, no eligibility
check, no lag tracking. Selection is leftmost-vruntime, not
earliest-eligible-deadline. The EEVDF roadmap further down replaces
the selection logic while keeping the same data structures.

### Schedulable entities

Three classes, picked in priority order inside `do_schedule()`:

1. **Worker threads** (`kernel/wth.c`). Bottom-half processing for
   IRQ handlers. Tracked in `worker_threads[]` sorted by priority,
   *not* in the runnable tree. `wth_get_runnable_thread()` runs
   first in `do_schedule()`: a runnable worker always wins over a
   runnable ordinary task. Workers have no timeslice; they run
   until they self-yield by draining their queue.

2. **Ordinary tasks** (kernel threads and user processes). Live in
   the runnable AVL tree (`runnable_tree_root`) keyed by
   `(vruntime, tid)`. Selection is `bintree_get_first_obj()` — the
   leftmost node, i.e. the task with the lowest vruntime. The tid
   tiebreaker is just to keep keys unique for the AVL invariant;
   it doesn't carry scheduling meaning.

3. **Idle task** (`idle_task` in `kernel/sched.c`). One kernel
   thread per system, running a `halt()` loop. *Not* in the tree:
   `do_schedule()` falls back to `idle_task` directly when the
   runnable tree is empty. Keeping idle outside the tree
   simplifies the curr-RUNNING → curr-RUNNABLE transition (no
   risk of re-inserting an already-in-tree node) and removes a
   "skip past idle" iteration from every selection.

### Per-task state

Per-task scheduler bookkeeping (`struct sched_ticks` in
`include/tilck/kernel/sched.h`):

  - `u32 timeslice` — subticks elapsed in the current slice.
    Reset at each `switch_to_task()`.
  - `u64 total` — total lifetime ticks the task was `RUNNING`
    (raw ticks, not subticks; used by selftests / stats).
  - `u64 total_kernel` — subset of `total` spent in kernel mode.
  - `atomic_u64_t vruntime` — virtual runtime in subticks. The
    tree key. Atomic because the writer (`sched_account_ticks`,
    in timer-IRQ context) and the reader (`sched_runnable_cmp`
    inside `bintree_*` calls) race.

### Subtick precision

vruntime and `timeslice` are stored in **subticks**:
`1 real tick = VRUNTIME_SCALE subticks`, with
`VRUNTIME_SCALE = 16` (`kernel/sched.c`). This is purely a math
trick to keep the dynamic-slice divide from truncating hard:
`SCHED_LATENCY_TICKS / nr_running` in raw ticks goes to the
`MIN_GRANULARITY_TICKS` floor at `nr_running > ~10`, but the
scaled version `SCHED_LATENCY_TICKS * VRUNTIME_SCALE / nr_running`
keeps ~0.06-tick (0.25 ms) granularity all the way down. 16 was
picked because it's a power of two (multiply and divide lower to
shifts) and 4 bits of sub-tick precision is enough at the
runnable counts Tilck actually hits.

`u64 vruntime` overflows at ~2^60 subticks, hundreds of years at
`KRN_TIMER_HZ=250`. The scale doesn't shorten the overflow horizon
in any practical sense.

### Tunables

In `config/kernel/config_sched.h` (and as CMake options in
`other/cmake/kernel_options.cmake`):

  - `KRN_TIMER_HZ` (default `250`) — timer IRQ frequency in Hz.
    Drives the PIT.
  - `KRN_SCHED_LATENCY_TICKS` (default `20`) — target maximum
    scheduling latency in real ticks. At default `KRN_TIMER_HZ`,
    `20 * 4 ms = 80 ms`.
  - `KRN_MIN_GRANULARITY_TICKS` (default `2`) — floor for the
    per-task slice. At default `KRN_TIMER_HZ`, `2 * 4 ms = 8 ms`.
  - `KRN_MINIMAL_TIME_SLICE` (default `OFF`) — stress-test
    override. When set, pins `SCHED_LATENCY_TICKS = 1` and
    `MIN_GRANULARITY_TICKS = 1` so every timer tick is a
    preemption point.

### Dynamic slice

Computed at every timer tick inside `sched_account_ticks()`:

```c
nr_running = runnable_tasks_count + 1;     /* +1 folds curr in */
slice      = max(SCHED_LATENCY_TICKS * VRUNTIME_SCALE / nr_running,
                 MIN_GRANULARITY_TICKS    * VRUNTIME_SCALE);
```

`runnable_tasks_count` is the size of the runnable tree (excluding
curr, idle, and workers); `+1` folds curr back in. Each running
task's `timeslice` grows by `VRUNTIME_SCALE` per real tick;
preemption fires when `timeslice >= slice`.

Slice table at default constants (`SCHED_LATENCY=20`,
`MIN_GRANULARITY=2`, `SCALE=16`):

| nr_running | slice (subticks) | slice (real ticks) | slice (ms) |
|-----------:|-----------------:|-------------------:|-----------:|
|          1 |              320 |              20.00 |         80 |
|          2 |              160 |              10.00 |         40 |
|          3 |              106 |               6.625 |        ~27 |
|          4 |               80 |               5.00 |         20 |
|          5 |               64 |               4.00 |         16 |
|          8 |               40 |               2.50 |         10 |
|        10+ |               32 |               2.00 |          8 |

The 2-task case (shell + one user task, the common interactive
load) yields a 40 ms slice — matching the previous fixed
`TIME_SLICE_TICKS=10` behavior exactly. Lighter load gets a longer
slice for cache locality; heavier load gets a shorter slice for
latency, clamped at 8 ms.

### vruntime accounting

`sched_account_ticks()`, called from the timer IRQ:

```c
t->timeslice += VRUNTIME_SCALE;
t->total++;

if (is_running && curr != idle_task) {
   atomic_fetch_add(&t->vruntime, VRUNTIME_SCALE);
   if (t->vruntime > min_vruntime) min_vruntime = t->vruntime;
}
```

vruntime grows by `VRUNTIME_SCALE` per tick of RUNNING time. Idle is
excluded — its CPU time is "free" (it represents the CPU having
nothing to do). The `is_running` gate is load-bearing: between
`task_change_state(curr, RUNNABLE)` inside `do_schedule()` and
`set_curr_task(selected)` inside `switch_to_task()`,
`get_curr_task()` still returns the outgoing task — which is now
RUNNABLE and *in the tree*. Mutating its vruntime in that window
would corrupt the tree key from under `bintree_remove()`.

`min_vruntime` is a monotonic high-watermark across all
RUNNABLE-task vruntimes. It never decreases. Consumed by the wake
and fork handoffs to keep newcomers and long-sleepers from
leapfrogging the leading edge.

### Wake and fork handoffs

`fork_vruntime_handoff()`: called when a fresh task is allocated
(`allocate_new_thread`, `allocate_new_process`). Sets the new
task's vruntime to `min_vruntime`. Without this, the new task
starts at 0 and dominates the CPU until it catches up to the rest.

`wake_vruntime_handoff()`: called when a SLEEPING task transitions
to RUNNABLE (`wake_up()` in `kernel/wobj.c`, `tick_all_timers()` in
`kernel/timer.c`). Raises vruntime to
`max(vruntime, min_vruntime - WAKEUP_VRUNTIME_BONUS)`, with an
underflow guard at 0. `WAKEUP_VRUNTIME_BONUS = 10 * VRUNTIME_SCALE`
(i.e. ten ticks' worth of head start, ~40 ms at default config),
giving woken tasks a small preference over already-runnable ones
to keep interactive workloads responsive without letting a
long-sleeper monopolize the CPU.

**Critical invariant:** vruntime must not change while the task is
in the runnable tree (state == RUNNABLE), because it's the tree
key. `wake_vruntime_handoff()` is therefore gated:

```c
disable_interrupts(&var);
{
   if (atomic_load(&ti->state) != TASK_STATE_SLEEPING)
      goto out;          /* idempotent: no-op while RUNNABLE/RUNNING */
   /* ... raise vruntime ... */
}
out:
   enable_interrupts(&var);
```

A double-wake (e.g. a kcond signaler racing the timer-driven wake
on the same wobj+timer waiter) reaches this function with state
already RUNNABLE and is silently turned into a no-op.

### Runnable tree

AVL tree, root `runnable_tree_root` (single pointer; bintree.h
encodes the rest in per-node fields). Keyed by `(vruntime, tid)`
via `sched_runnable_cmp()`. Insert/remove happens inside
`task_add_to_state_list()` / `task_remove_from_state_list()`,
called from `task_change_state_unsafe()`.

A few gotchas worth knowing because they cost real debugging time:

  - **Idle is *not* in the tree.** Init-order workaround in
    `init_sched()`: `kthread_create(idle, ...)` runs with
    `idle_task` still NULL, so the very first `add_task()` falls
    through to the RUNNABLE case and inserts idle. `init_sched()`
    pulls it back out immediately after assigning `idle_task`,
    and the `ti == idle_task` guards in
    `task_add/remove_from_state_list` keep it out for good.
  - **`bintree_node_init()` before every insert.** `bintree_insert`
    places `ti` at a slot but does NOT clear ti's own bintree_node;
    after a previous insert/remove cycle, the stale `left/right`
    pointers would survive into the next insert and corrupt
    subsequent removes. The runnable tree is the only one in the
    file that sees a node come and go on every state transition,
    so this guard is here and nowhere else.
  - **All tree mutations run with interrupts disabled.** `add_task()`
    uses `disable_interrupts()`, not just `disable_preemption()`,
    because an IRQ-driven `tick_all_timers()` may insert into the
    same tree concurrently with `add_task()`'s insert, and AVL
    rotations briefly leave links inconsistent. The list-based
    predecessor didn't need this; the AVL does.
  - **The selection walk also disables interrupts.** Same reason:
    `bintree_get_first_obj()` descends through `LEFT_OF()` chains;
    catching a rotation mid-flight reads a stale pointer.

### State-transition machinery

State live-ness w.r.t. the runnable tree:

| State    | In runnable tree? | In timer wakeup list?  |
|----------|-------------------|------------------------|
| RUNNABLE | yes               | no                     |
| RUNNING  | no (it's curr)    | no                     |
| SLEEPING | no                | maybe (kernel_sleep)   |
| STOPPED  | no                | no                     |
| ZOMBIE   | no                | no                     |

`task_change_state_unsafe()` always does:

```
task_remove_from_state_list(ti);   /* uses CURRENT state */
atomic_store(&ti->state, new_state);
task_add_to_state_list(ti);        /* uses NEW state */
```

`task_change_state()` wraps the above in `disable_interrupts()`.
`task_change_state_idempotent()` skips the call if state already
matches.

### Idle and the empty-tree path

`idle()` runs `halt()` in a loop and re-enters `schedule()` when
either `need_reschedule()` or `get_runnable_tasks_count() > 0`.
The latter is the wake path: an IRQ-driven wake inserts a task,
and the next return from `halt()` notices the count and yields.

`do_schedule()` falls back to `idle_task` when the runnable tree
is empty *and* the outgoing curr can't be kept (not RUNNING). When
the outgoing curr is RUNNING and the tree is empty, it just keeps
running curr.

### Validation

`tests/self/se_fairness.c` (`selftest_fairness`): five equal-weight
CPU-bound kthreads, 2-second window, asserts per-thread `ticks.total`
spread stays within 10% of the mean (25% under the
`KRN_MINIMAL_TIME_SLICE` stress build). Empirically the default
build measures 2-3% spread, comfortably under the bound.

The test's header comment enumerates the five assumptions whose
violation would render the verdict misleading — read those before
trusting a regression signal from this test.

---

## EEVDF roadmap

The aim is to replace the leftmost-vruntime selection with the
EEVDF rule: among **eligible** tasks, pick the one with the
**earliest virtual deadline**. The AVL substrate, vruntime,
min_vruntime, and the wake/fork handoffs all stay — EEVDF is a
re-keying of the same machinery, not a rewrite.

### Why EEVDF over plain CFS

Plain CFS (which is what we have now) gives equal long-run shares
but is vague about short-run latency: a freshly-woken task gets
preference via the wake handoff bonus, but the *amount* of
preference is hand-tuned (`WAKEUP_VRUNTIME_BONUS`) and there's no
machinery to bound how much one task can be ahead of another
within a slice.

EEVDF formalizes this. Each task carries a **slice request** —
how much CPU it expects to consume per turn — and from that
derives an **eligible time** (the earliest vruntime at which the
task is "ready to claim its share") and a **virtual deadline**
(the latest vruntime by which it must finish that share). The
scheduler picks the eligible task with the earliest deadline.
That gives:

  - Bounded lag: a task can be at most `slice` of vruntime ahead
    of the fair share, by construction.
  - Wake preemption is principled: the woken task is eligible
    immediately (assuming its lag isn't already negative) and its
    deadline competes directly with curr's.
  - Slice requests can vary per task: a latency-sensitive task
    can request a small slice and be preempted more often; a
    throughput-oriented task can request a larger one. Equal slices
    today, room to differentiate tomorrow.

Equal-weight assumption stays for the foreseeable future. Weighted
EEVDF (the nice / prio knob) would re-introduce per-task weights
into `avg_vruntime` and slice budgeting. Out of scope here.

### Concepts

  - **vruntime** — same as today. Already in subticks. Advances
    `VRUNTIME_SCALE` per real tick of RUNNING time.
  - **slice request** (`u32 slice` in `struct sched_ticks`) — how
    much vruntime this task is asking for per turn. Equal-weight
    case: every task asks for `SCHED_LATENCY_TICKS * VRUNTIME_SCALE
    / nr_running`, i.e. the same value the current dynamic-slice
    formula already produces. Computed at enqueue time, not every
    tick.
  - **virtual deadline** (`u64 vdeadline` in `struct sched_ticks`)
    — `vruntime + slice` at the moment the task became RUNNABLE.
    Selection key: smallest deadline among eligible tasks.
  - **avg_vruntime** — weighted average of vruntime across all
    runnable tasks. Equal weights → arithmetic mean. Maintained
    incrementally: on insert, `sum += vruntime; n++`; on remove,
    the inverse. `mean = sum / n`.
  - **lag** — `avg_vruntime - vruntime`. Positive: task has
    received less than its fair share; eligible. Negative: task
    has received more than its share; ineligible until the
    average catches up. (For the equal-weight case we don't need
    to *store* lag — we can recompute it from `avg_vruntime` and
    the task's vruntime on demand.)
  - **eligible** — `lag >= 0`, equivalently `vruntime <= avg_vruntime`.

### Selection invariant

> Among RUNNABLE tasks with `vruntime <= avg_vruntime`, pick the
> one with the smallest `vdeadline`. Break ties on tid.

The current "leftmost in `(vruntime, tid)`" rule becomes "leftmost
in `(vdeadline, tid)` among the eligible subset". The eligibility
filter is the new piece.

### Substrate reuse and what changes

Reused unchanged:

  - The AVL tree as the runnable container. Just re-keyed.
  - `runnable_tasks_count`, the worker-thread and idle-task
    bypasses.
  - vruntime, min_vruntime, `WAKEUP_VRUNTIME_BONUS`,
    `fork_vruntime_handoff` / `wake_vruntime_handoff`.
  - The subtick precision factor `VRUNTIME_SCALE`.
  - `SCHED_LATENCY_TICKS`, `MIN_GRANULARITY_TICKS`,
    `KRN_MINIMAL_TIME_SLICE`.
  - `sched_account_ticks()`'s per-tick increments. The
    `is_running` gate stays load-bearing.

What changes:

  - `struct sched_ticks` gains `slice` and `vdeadline`.
  - The runnable AVL is keyed by `(vdeadline, tid)`, not
    `(vruntime, tid)`.
  - `sched_runnable_cmp()` compares `vdeadline` instead of
    `vruntime`. Otherwise structurally identical.
  - A new `static u64 avg_vruntime` tracked via insert / remove
    deltas — or, equivalently, `static u64 vruntime_sum;` with the
    count read from `runnable_tasks_count`.
  - `sched_do_select_runnable_task()` walks the leftmost subtree
    skipping nodes with `vruntime > avg_vruntime` (ineligible),
    returns the first eligible leftmost. In the common case the
    leftmost-by-deadline is already eligible and the skip is a
    no-op.
  - On every state-change-to-RUNNABLE (wake, fork, preemption),
    set `vdeadline = vruntime + slice` *before* inserting into the
    tree. The slice value is computed at this moment from
    `nr_running` (which is `runnable_tasks_count + 1`).

What goes away:

  - The dynamic-slice computation inside `sched_account_ticks()`
    moves to enqueue time (insert) and operates on `slice` /
    `vdeadline`. The `t->timeslice >= slice` comparison stays for
    the "this turn is up, set need_resched" logic, but the slice
    is now a per-task value already in the struct, not a quantity
    recomputed per tick.
  - The `WAKEUP_VRUNTIME_BONUS` head start is subsumed by the
    eligibility / deadline mechanism: a woken task that's
    eligible AND has the earliest deadline naturally preempts;
    nothing else needs to lean on it. The constant probably gets
    deleted (or held at 0) once the wake path is on EEVDF.

### Step-by-step plan

Each step is meant to be a single self-contained commit
(bisectable; project rule). The neutral-or-better property from
the CFS roadmap holds again: at no point should the scheduler get
worse than what's in master today.

1. **Add `slice` and `vdeadline` to `struct sched_ticks`.**
   Populate them at insert (using the same formula as today's
   dynamic slice), but don't use them for selection yet. Pure
   bookkeeping commit.

2. **Track `avg_vruntime` (or `vruntime_sum`).** Maintain on
   insert / remove. Add a debug printk on the selection path that
   reports the eligibility set so we can verify the math before
   the selection rule changes.

3. **Switch the AVL key from `vruntime` to `vdeadline`.**
   `sched_runnable_cmp()` compares `vdeadline` instead. Selection
   still uses leftmost. Behavior changes (a task with a larger
   vruntime but earlier deadline now beats one with smaller
   vruntime but later deadline), but in the equal-slice case
   today the deadlines mirror the vruntimes plus a constant
   offset, so this should be neutral.

4. **Add the eligibility check to selection.** Walk in-order from
   the leftmost (i.e. smallest deadline). Return the first node
   whose `vruntime <= avg_vruntime`. In the common case this is
   the leftmost itself.

5. **Drop the wake-handoff bonus from the selection path.** The
   eligibility + deadline machinery now subsumes its role. Keep
   `wake_vruntime_handoff` as the "don't let long-sleepers
   dominate" guard — the math is the same; the rationale narrows.

6. **Drop the dynamic-slice computation in
   `sched_account_ticks()`.** Replace with `t->timeslice >=
   t->slice` (per-task slice now in the struct). Slice is set
   once at enqueue time, not every tick.

7. **Extend `tests/self/se_fairness.c`** to also assert that
   no thread ever runs for more than `slice` ticks consecutively
   without yielding to a peer. Plus a new selftest for wake
   preemption: a CPU-bound thread alongside a sleep-loop thread;
   the sleep-loop thread should preempt the CPU-bound thread
   "immediately" (within `min_granularity`) on wake.

### Open design questions

  - **Where does the slice get recomputed?** Today the dynamic
    slice scales with `nr_running` each tick; in EEVDF the slice
    is fixed at enqueue. If `nr_running` changes mid-slice
    (another task wakes), the in-flight curr's slice doesn't
    shrink to match — that has fairness implications for bursty
    fork/exit loads. Linux refreshes slice on wakeup if it's
    "too big" relative to current nr_running. Decide whether
    Tilck wants the same heuristic or a simpler "slice is sticky
    until the next dequeue" policy. Probably the simpler one
    given Tilck's scale.

  - **Granularity of `avg_vruntime`.** Equal weights → arithmetic
    mean → exact integer. With weights (future), the mean is a
    weighted sum that doesn't divide cleanly — Linux uses a
    fixed-point representation. Not a problem until weights enter.

  - **The walker for eligibility.** Walking from leftmost-deadline
    and skipping ineligible nodes is O(N) worst-case, but in
    practice the leftmost is usually already eligible (eligibility
    is "behind average"; the leftmost-deadline node is also the
    one most behind on vruntime among recent inserts). If profiling
    ever shows the walker as a hot spot, the augmented-AVL trick
    (each node carries `min(vruntime)` of its subtree, prune
    ineligible subtrees in O(log N)) is the standard remedy. Not
    needed for Tilck's task counts as a first cut.

### Validation

Same shape as the CFS roadmap: each step builds and tests under
`./build/st/run_all_tests -c` plus `./build/gtests` plus the
`gen_other_builds` configurations. `se_fairness` keeps guarding
the equal-share contract through the transition; the new
preemption-on-wake selftest catches the EEVDF-specific
properties.

---

## Forward-looking: realtime classes (NOT designed here)

Tilck's longer-term direction is to grow hard-realtime support: a
separate scheduling class for realtime tasks that strictly
preempts the EEVDF class, with tight bounds on the latency
between a realtime task becoming runnable and it actually running.

That design is out of scope for both this document and the EEVDF
work above. The relevant point for the EEVDF roadmap is that
**all tasks remain equal-weight** in the EEVDF class — there's no
nice / priority knob within EEVDF. The "priority" lever for
realtime is the class boundary, not weights inside the class.
When the realtime plan does come together, it slots in cleanly:

  - A new selection pass in `do_schedule()` *before* the worker
    pass (or possibly co-equal with it), checking for runnable
    realtime tasks.
  - Realtime tasks live in their own container (probably not the
    EEVDF AVL — sorted by realtime priority or deadline, depending
    on the realtime model chosen).
  - The EEVDF class becomes "background" — runs only when no
    realtime task wants the CPU.

Nothing in the EEVDF roadmap above forecloses this. The realtime
plan will get its own document when the time comes.
