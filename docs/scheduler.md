# Tilck scheduler

Companion to `kernel/sched.c`. Captures the *current* scheduler — a
**collapsed-EEVDF** instantiation: the full Earliest Eligible Virtual
Deadline First algorithm, with all tasks pinned at weight = 1 and a
uniform per-quantum slice budget. The full algorithm is implemented;
the parameters are pinned. Lifting either constraint (per-task slice,
per-task weights) is local change at the formula sites identified in
the [code-site index](#code-site-index), not an algorithmic rewrite.

This document is meant to be readable cold, by someone returning to
scheduler code after weeks away. Skim "Algorithm" first; the rest
fills in details.

## Algorithm

Tilck has a single-CPU, preemptive, fair-share scheduler. Selection
follows the EEVDF rule:

> Among **eligible** runnable tasks, pick the one with the
> **earliest virtual deadline**.

Eligibility and deadline are derived per-task from `vruntime` (virtual
runtime, advances with consumed CPU), a per-task `slice` (the quantum
budget), and a global `avg_vruntime` (the weighted-mean reference).

### General EEVDF (the math)

For each task `i`:

- `vruntime_i` = virtual runtime, advances by `1/w_i` per unit of real
  CPU consumed.
- `slice_i` = the task's CPU-quantum request, in vruntime units.
- `deadline_i = vruntime_i + slice_i / w_i` = the virtual time by
  which this task expects its current quantum to complete.

Across all runnable tasks (including curr):

- `V = sum(w_i · vruntime_i) / sum(w_i)` = the weighted-mean virtual
  time. The reference clock against which lag is measured.
- `lag_i = V - vruntime_i` = how much CPU this task is owed (positive
  = owed more, negative = got too much).
- Task `i` is **eligible** iff `lag_i ≥ 0`, equivalently
  `vruntime_i ≤ V`.

Selection rule: among eligible tasks, return `argmin(deadline_i)`.

### Equal-weight + uniform-slice collapse (what Tilck does today)

Tilck pins `w_i = 1` ∀i and `slice_i = SCHED_LATENCY / N` clamped at
`MIN_GRAN`, where `N = runnable_tasks_count + 1` (curr folded in).
Substituting:

- `vruntime_i` advances by `1` per real tick (`VRUNTIME_SCALE` per
  tick in subticks; see [subtick precision](#subtick-precision)).
- `slice_i = slice` (constant across all tasks at any given moment).
- `deadline_i = vruntime_i + slice` — a uniform constant offset above
  `vruntime`.
- `V = mean(vruntime over runnable + curr)` — simple arithmetic mean.
- Eligibility: `vruntime_i ≤ V`.

Two consequences make the implementation simpler than the general
algorithm:

1. **Order by deadline ≡ order by vruntime.** With `slice` a uniform
   constant offset, sorting by `vruntime` and sorting by `deadline`
   produce identical orderings. The runnable AVL is keyed by
   `(vruntime, tid)`, and `bintree_get_first_obj()` returns the
   min-vruntime task — which is also the min-deadline task.

2. **The min-vruntime task is always eligible.** By definition, the
   minimum value of a set is at or below the set's mean. So the
   leftmost-of-tree is always at or below `V`, hence always eligible.

⇒ Selection collapses to a single `bintree_get_first_obj()` call in
O(log N). When per-task slice or weight diverges, these two properties
break and the selector needs either an in-order walk skipping
ineligibles, or an augmented AVL (subtree-min-eligible-deadline).
Both are deferred. See [future relaxations](#future-relaxations).

### Eligibility-predicate precision (limitation, by choice)

The implemented predicate is the literal `vruntime_i ≤ avg_vruntime`
check, computed against the simple mean of vruntimes over runnable +
curr. This is a coarse approximation of the lag-based eligibility the
general algorithm uses, and it can transiently disagree with the
"true" eligibility at the wake-handoff boundary:

- Scenario: a long-running task `R` accumulates vruntime up to
  `min_vruntime = M`, then a peer `W` wakes after a long sleep.
  `wake_vruntime_handoff` raises `W` to `M - BONUS` (see [handoffs](
  #wake-and-fork-handoffs)). After `W` becomes curr and runs a few
  ticks, `W`'s vruntime is around `M - BONUS + small`, *below* `R`'s
  in-tree vruntime `M`. The simple mean of `(W.v + R.v)/2` lands
  between `W.v` and `R.v` — and below `R.v`. So `R` is flagged
  ineligible by the simple predicate.

The selector's "if no eligible task, pick leftmost anyway" fallback
absorbs this: `R` (the only candidate) is picked regardless. Behavior
is correct. The predicate is just temporarily lying.

A precise predicate would need:

- A per-task **lag** counter accumulated lazily, not derived on
  demand. The current `vruntime - avg_vruntime` form computes lag at
  read-time from inconsistent snapshots; the accurate form computes
  lag at each enqueue/dequeue boundary against the V at that moment
  and amortizes the delta.
- An **avg_vruntime with weighted-load tracking** rather than a simple
  arithmetic mean — so the wake-handoff's vruntime jolt doesn't
  destabilize V for tasks already in the tree.

Worth implementing only once per-task slices or weights actually
land — at that point the simple predicate becomes user-visible wrong,
not just structurally approximate. Tilck's current testing measures
the rate of "ineligible picks" in the workload simulator
([tests/unit/sched_test.cpp](../tests/unit/sched_test.cpp), Category
5) and bounds it loosely: < 1% for steady-state, < 10% during
wake-handoff transients, ≤ 4 ticks for boot-from-idle artifacts. A
sustained rate above those bounds is a real signal worth chasing; the
isolated handful of transient false-negatives is expected.

## Schedulable entities

Three classes, picked in priority order in `do_schedule()`:

1. **Worker threads** (`kernel/wth.c`). Bottom-half processing for
   IRQ handlers. Tracked in `worker_threads[]` sorted by priority,
   *not* in the runnable tree. `wth_get_runnable_thread()` runs
   first in `do_schedule()`: a runnable worker always wins over a
   runnable ordinary task. Workers have no slice budget — they run
   until they self-yield by draining their queue, or get preempted
   only by a higher-priority worker waking up.

2. **Ordinary tasks** (kernel threads and user processes). Live in
   the runnable AVL tree (`runnable_tree_root`) keyed by
   `(vruntime, tid)`. The tid tiebreaker keeps keys unique for the
   AVL invariant; it doesn't carry scheduling meaning.

3. **Idle task** (`idle_task` in `kernel/sched.c`). One kernel
   thread per system, running a `halt()` loop. *Not* in the tree:
   `do_schedule()` falls back to `idle_task` directly when the
   runnable tree is empty. Keeping idle outside the tree simplifies
   the curr-RUNNING → curr-RUNNABLE transition (no risk of
   re-inserting an already-in-tree node) and removes a "skip past
   idle" iteration from every selection.

## Per-task state

In `struct sched_ticks` (`include/tilck/kernel/sched.h`):

| Field          | Type             | Meaning |
|----------------|------------------|---------|
| `slice_used`   | `u32`            | Subticks consumed in the current quantum. Reset at quantum start. |
| `slice`        | `u32`            | Quantum budget. Set at quantum start, frozen for the quantum. |
| `total`        | `u64`            | Lifetime ticks the task was `RUNNING` (raw ticks, for stats). |
| `total_kernel` | `u64`            | Subset of `total` spent in kernel mode. |
| `vruntime`     | `atomic_u64_t`   | Virtual runtime in subticks. The tree key. |
| `deadline`     | `atomic_u64_t`   | `vruntime + slice` at the latest RUNNABLE-entry or quantum start. |

Per-task `atomic_u64_t` fields are atomic because the writer
(`sched_account_ticks`) runs in timer-IRQ context while readers
(comparators, eligibility checks) run with preemption disabled but
interrupts not always masked. The atomic wrapper provides
indivisibility plus the volatile cast that defeats compiler hoisting.

## Subtick precision

`vruntime`, `slice_used`, `slice`, and `deadline` are stored in
**subticks**: `1 real tick = VRUNTIME_SCALE subticks`, with
`VRUNTIME_SCALE = 16` (`kernel/sched.c`).

This keeps the dynamic-slice divide from truncating hard:
`SCHED_LATENCY_TICKS / nr_running` in raw ticks collapses to the
`MIN_GRANULARITY_TICKS` floor at `nr_running > ~10`, but the scaled
version `SCHED_LATENCY_TICKS * VRUNTIME_SCALE / nr_running` keeps
~0.06-tick (0.25 ms) granularity all the way down. 16 was chosen
because it's a power of two (multiply/divide lower to shifts) and 4
bits of sub-tick precision is enough at the runnable counts Tilck
actually hits.

`u64 vruntime` overflows at ~2^60 subticks, hundreds of years at
`KRN_TIMER_HZ=250`. The scale doesn't shorten the overflow horizon in
any practical sense.

## Tunables

In `config/kernel/config_sched.h` (and as CMake options in
`other/cmake/kernel_options.cmake`):

- `KRN_TIMER_HZ` (default `250`) — timer IRQ frequency in Hz.
  Drives the PIT.
- `KRN_SCHED_LATENCY_TICKS` (default `20`) — target maximum
  scheduling latency in real ticks. At default `KRN_TIMER_HZ`,
  `20 × 4 ms = 80 ms`.
- `KRN_MIN_GRANULARITY_TICKS` (default `2`) — floor for the
  per-task slice. At default `KRN_TIMER_HZ`, `2 × 4 ms = 8 ms`.
- `KRN_MINIMAL_TIME_SLICE` (default `OFF`) — stress-test
  override. When set, pins `SCHED_LATENCY_TICKS = 1` and
  `MIN_GRANULARITY_TICKS = 1` so every timer tick is a
  preemption point.

## Slice computation

`sched_compute_slice()` returns the dynamic per-quantum budget:

```c
nr_running = runnable_tasks_count + 1;   /* +1 folds curr in */
slice      = max(SCHED_LATENCY_TICKS * VRUNTIME_SCALE / nr_running,
                 MIN_GRANULARITY_TICKS    * VRUNTIME_SCALE);
```

`runnable_tasks_count` is the size of the runnable tree (excluding
curr, idle, and workers); `+1` folds curr back in. Called whenever a
task's slice + deadline are refreshed (`sched_refresh_slice_deadline`)
or a new quantum begins (`sched_start_quantum`).

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

The 2-task case (shell + one user task, the common interactive load)
yields a 40 ms slice. Lighter load gets a longer slice for cache
locality; heavier load gets a shorter slice for latency, clamped at
8 ms.

## State maintenance

### Quantum start

`sched_start_quantum(ti)` (resets `slice_used`, refreshes `slice` +
`deadline`) fires when:

- `switch_to_task(ti)` makes `ti` the new curr.
- `do_schedule()`'s keep-curr branch decides to extend curr.
- `create_kernel_process()` seeds the boot task that becomes curr
  without going through `switch_to_task`.

The slice is **frozen for the duration of the quantum**: a wake or
sleep mid-quantum may change `nr_running`, but the running task's
slice stays at the value it was given at quantum start. This is
EEVDF-correct (the quantum is the lag-bounding unit; refairing
mid-quantum violates the bound).

### Per-tick increment

`sched_account_ticks()`, called from the timer IRQ:

```c
t->slice_used += VRUNTIME_SCALE;
t->total++;

if (is_running && curr != idle_task) {
    atomic_fetch_add(&t->vruntime, VRUNTIME_SCALE);

    if (t->vruntime > min_vruntime)
        min_vruntime = t->vruntime;
}

timeout = !is_worker && t->slice_used >= t->slice;

if (!is_running || timeout)
    sched_set_need_resched();
```

`vruntime` grows by `VRUNTIME_SCALE` per tick of RUNNING time. Idle is
excluded — its CPU time is "free". The `is_running` gate is
load-bearing: between `task_change_state(curr, RUNNABLE)` inside
`do_schedule()` and `set_curr_task(selected)` inside
`switch_to_task()`, `get_curr_task()` still returns the outgoing task
— which is now RUNNABLE and *in the tree*. Mutating its vruntime in
that window would corrupt the tree key from under `bintree_remove()`.

`min_vruntime` is a monotonic high-watermark across all `RUNNING`
vruntimes seen. It never decreases. Consumed by the wake and fork
handoffs to keep newcomers and long-sleepers from leapfrogging the
leading edge.

### sum_vruntime_in_tree maintenance

`sum_vruntime_in_tree` is the sum of `vruntime` over tasks currently
in the runnable tree (excludes curr). Maintained at the same two
points that maintain `runnable_tasks_count`:

- Insert (`task_add_to_state_list`, RUNNABLE case):
  `atomic_fetch_add(&sum_vruntime_in_tree, ti->vruntime)`.
- Remove (`task_remove_from_state_list`, RUNNABLE case):
  `atomic_fetch_sub(&sum_vruntime_in_tree, ti->vruntime)`.
- Plus a mirror in `init_sched` for the direct `bintree_remove(idle)`
  that bypasses `task_remove_from_state_list`.

`sched_compute_avg_vruntime()` reads
`(sum_vruntime_in_tree + curr->vruntime) / nr_running` on demand. No
per-tick maintenance — the read picks up curr's increment
automatically.

### Wake and fork handoffs

`fork_vruntime_handoff(ti)`: called when a fresh task is allocated
(`allocate_new_thread`, `allocate_new_process`). Sets the new task's
vruntime to `min_vruntime`, then refreshes its `slice` and `deadline`.
Without the vruntime handoff, a fresh task starts at 0 and dominates
the CPU until it catches up to the rest.

`wake_vruntime_handoff(ti)`: called when a SLEEPING task transitions
to RUNNABLE (`wake_up()` in `kernel/wobj.c`, `tick_all_timers()` in
`kernel/timer.c`). Raises vruntime to
`max(vruntime, min_vruntime - WAKEUP_VRUNTIME_BONUS)`, with an
underflow guard at 0, then refreshes slice/deadline.
`WAKEUP_VRUNTIME_BONUS = 10 × VRUNTIME_SCALE` (ten ticks' worth of
head start, ~40 ms at default config), giving woken tasks a small
preference over already-runnable ones to keep interactive workloads
responsive without letting a long-sleeper monopolize the CPU.

**Critical invariant:** vruntime must not change while the task is in
the runnable tree (state == RUNNABLE), because it's the tree key.
`wake_vruntime_handoff()` is therefore gated:

```c
disable_interrupts(&var);
{
   if (atomic_load(&ti->state) != TASK_STATE_SLEEPING)
      goto out;          /* idempotent: no-op while RUNNABLE/RUNNING */
   /* ... raise vruntime, refresh slice/deadline ... */
}
out:
   enable_interrupts(&var);
```

A double-wake (e.g. a kcond signaler racing the timer-driven wake on
the same wobj+timer waiter) reaches this function with state already
RUNNABLE and is silently turned into a no-op.

### Deadline refresh on preemption

When `do_schedule()` preempts curr (curr_state == RUNNING and the
selector picked someone else), it refreshes curr's slice+deadline
before transitioning curr to RUNNABLE. The quantum just ended, vruntime
grew during it, and the new `deadline = new_vruntime + new_slice` is
what an EEVDF selector will compare against on the next pick.

## Runnable tree

AVL tree, root `runnable_tree_root` (single pointer; `bintree.h`
encodes the rest in per-node fields). Keyed by `(vruntime, tid)` via
`sched_runnable_cmp()`. Insert/remove happens inside
`task_add_to_state_list()` / `task_remove_from_state_list()`, called
from `task_change_state_unsafe()`.

A few gotchas worth knowing because they cost real debugging time:

- **Idle is *not* in the tree.** Init-order workaround in
  `init_sched()`: `kthread_create(idle, ...)` runs with `idle_task`
  still NULL, so the very first `add_task()` falls through to the
  RUNNABLE case and inserts idle. `init_sched()` pulls it back out
  immediately after assigning `idle_task`, and the `ti == idle_task`
  guards in `task_add/remove_from_state_list` keep it out for good.
- **`bintree_node_init()` before every insert.** `bintree_insert`
  places `ti` at a slot but does NOT clear ti's own `bintree_node`;
  after a previous insert/remove cycle, the stale `left/right`
  pointers would survive into the next insert and corrupt subsequent
  removes. The runnable tree is the only one in the file that sees a
  node come and go on every state transition, so this guard is here
  and nowhere else.
- **All tree mutations run with interrupts disabled.** `add_task()`
  uses `disable_interrupts()`, not just `disable_preemption()`,
  because an IRQ-driven `tick_all_timers()` may insert into the same
  tree concurrently with `add_task()`'s insert, and AVL rotations
  briefly leave links inconsistent.
- **The selection walk also disables interrupts.** Same reason:
  `bintree_get_first_obj()` descends through `LEFT_OF()` chains;
  catching a rotation mid-flight reads a stale pointer.

## State-transition machinery

State live-ness w.r.t. the runnable tree:

| State    | In runnable tree? | In timer wakeup list? |
|----------|-------------------|-----------------------|
| RUNNABLE | yes               | no                    |
| RUNNING  | no (it's curr)    | no                    |
| SLEEPING | no                | maybe (kernel_sleep)  |
| STOPPED  | no                | no                    |
| ZOMBIE   | no                | no                    |

`task_change_state_unsafe()` always does:

```c
task_remove_from_state_list(ti);   /* uses CURRENT state */
atomic_store(&ti->state, new_state);
task_add_to_state_list(ti);        /* uses NEW state */
```

`task_change_state()` wraps the above in `disable_interrupts()`.
`task_change_state_idempotent()` skips the call if state already
matches.

## Idle and the empty-tree path

`idle()` runs `halt()` in a loop and re-enters `schedule()` when
either `need_reschedule()` or `get_runnable_tasks_count() > 0`. The
latter is the wake path: an IRQ-driven wake inserts a task, and the
next return from `halt()` notices the count and yields.

`do_schedule()` falls back to `idle_task` when the runnable tree is
empty *and* the outgoing curr can't be kept (not RUNNING). When the
outgoing curr is RUNNING and the tree is empty, it just keeps running
curr.

## Code-site index

Quick lookup table for "where is X done?". Helpful when relaxing a
constraint or chasing a regression.

| Concept                            | File                          | Symbol |
|------------------------------------|-------------------------------|--------|
| Selection                          | `kernel/sched.c`              | `sched_do_select_runnable_task` |
| Top-level schedule                 | `kernel/sched.c`              | `do_schedule` |
| Per-tick accounting                | `kernel/sched.c`              | `sched_account_ticks` |
| Quantum start (resets slice_used)  | `kernel/sched.c`              | `sched_start_quantum` |
| Slice budget computation           | `kernel/sched.c`              | `sched_compute_slice` |
| Deadline refresh                   | `kernel/sched.c`              | `sched_refresh_slice_deadline` |
| Eligibility predicate              | `kernel/sched.c`              | `sched_is_eligible` |
| avg_vruntime computation           | `kernel/sched.c`              | `sched_compute_avg_vruntime` |
| Fork vruntime handoff              | `kernel/sched.c`              | `fork_vruntime_handoff` |
| Wake vruntime handoff              | `kernel/sched.c`              | `wake_vruntime_handoff` |
| Tree insert/sum maintenance        | `kernel/sched.c`              | `task_add_to_state_list` |
| Tree remove/sum maintenance        | `kernel/sched.c`              | `task_remove_from_state_list` |
| min_vruntime high-watermark        | `kernel/sched.c`              | `min_vruntime` (file-scope) |
| sum_vruntime_in_tree               | `kernel/sched.c`              | `sum_vruntime_in_tree` |
| Per-task scheduler state struct    | `include/tilck/kernel/sched.h`| `struct sched_ticks` |
| Test-only handles for unit tests   | `include/tilck/kernel/test/sched.h` | `STATIC` declarations + extern symbols |

## Validation

Three test layers, in increasing wall-clock cost.

### Unit tests (`tests/unit/sched_test.cpp`)

GoogleTest, ~3 s on a laptop. Five categories:

1. **Tick accounting** — `sched_account_ticks` increments,
   `min_vruntime` monotonicity.
2. **Dynamic slice / timeout** — `quantum_ends_at_dynamic_slice`,
   `slice_clamps_at_min_granularity`, `non_running_curr_resched_fires_immediately`.
3. **Selection** — leftmost pick, tid tiebreak, the curr-keep paths,
   and the wake-handoff corner that exercises EEVDF's "no eligible
   → leftmost" fallback (`select_picks_leftmost_even_if_ineligible_by_simple_predicate`).
4. **Fork / wake handoffs + avg_vruntime + eligibility** —
   `sum_vruntime_in_tree` insert/remove tracking,
   `avg_vruntime_is_mean_of_runnable_and_curr`, eligibility-predicate
   boundary cases.
5. **Workload-driven fairness simulator** — N synthetic ticks, fixed
   event vector (sleep / wake / fork timings). Runs three scenarios:
   `equal_weight_all_runnable`, `post_wake_sleeper_does_not_dominate`,
   `fresh_fork_does_not_leapfrog`. Each asserts per-task share within
   tolerance plus loosely bounds the count of `ineligible_picks` (a
   non-zero count is informative — usually a wake-handoff transient —
   not a bug).

### Real-kernel selftests (`tests/self/`)

Run via `./build/st/run_all_tests`. The two scheduler-focused ones:

- `selftest_fairness` (`se_fairness.c`) — five equal-weight CPU-bound
  kthreads, 2-second window, asserts per-thread `total` spread stays
  within 10% of the mean (25% under `KRN_MINIMAL_TIME_SLICE`).
  Empirically the default build measures 2-3% spread. The test's
  header comment enumerates the five assumptions whose violation
  would render the verdict misleading.
- `selftest_eevdf` (`se_eevdf.c`) — fast EEVDF tripwire (~0.5 s
  window, 3 threads). Checks structural invariants per thread at
  exit: `slice` within the dynamic-slice clamp, `vruntime` advanced
  past zero, no starvation, loose 50 % spread bound. If this fails
  *alone* (with `selftest_fairness` still passing), suspect a
  localized EEVDF regression.

### Build matrix

Cross-config and cross-arch builds via `./scripts/adv/gen_other_builds`.
Each EEVDF roadmap step was verified against `gcc_rel` and `riscv64`
UBSAN configurations in addition to the default i386 debug build.

## Future relaxations

The collapsed-EEVDF instantiation is a deliberate stopping point.
Each relaxation below is localized: it touches the formula sites
identified in the [code-site index](#code-site-index) but doesn't
restructure the algorithm.

### Per-task slice requests (e.g. `sched_setattr`-style API)

What changes:

- `slice` becomes a per-task **request**, populated from a user-space
  API (or a kernel-side heuristic — interactive tasks request small,
  throughput tasks request large) rather than the uniform dynamic
  formula. `sched_compute_slice()` becomes a *default* used only at
  fork; subsequent quanta use the task's request.
- Deadline order decouples from vruntime order. The leftmost-of-tree
  by `(vruntime, tid)` is no longer guaranteed to be the min-deadline
  task. The selector grows an in-order walk that picks min-deadline
  among eligibles. Worst case O(N); for Tilck's runnable counts that's
  fine as a first cut.
- For an O(log N) selector, augment the AVL with subtree-min-eligible-
  deadline. The `bintree.h` API doesn't expose a rotation callback
  today; supporting augmentation cleanly needs either a small generic
  hook there or — as a contained alternative — duplicating a small
  EEVDF-aware AVL inside `sched.c`. This is a design decision worth
  re-opening at that point.

What stays the same: handoffs, vruntime accounting, the tree
structure (just augmented).

### Per-task weights (nice / priority within the EEVDF class)

What changes:

- vruntime advances by `WEIGHT_SCALE / w_i` per tick, not by a
  constant. The current `atomic_fetch_add(&vruntime, VRUNTIME_SCALE)`
  becomes `atomic_fetch_add(&vruntime, VRUNTIME_SCALE * WEIGHT_SCALE / w_i)`.
- `sched_compute_avg_vruntime` becomes a weighted mean:
  `sum(w_i × vruntime_i) / sum(w_i)`. `sum_vruntime_in_tree` becomes
  `sum(w_i × vruntime_i)`; a new `sum_weight_in_tree` (with the same
  insert/remove maintenance) tracks `sum(w_i)`.
- Deadline derives from `vruntime + slice / w_i`. The simple `+ slice`
  becomes `+ slice / w_i` at the four refresh sites.
- The eligibility predicate `vruntime ≤ avg_vruntime` is unchanged in
  form, but `avg_vruntime` is now the weighted mean.

What stays the same: the algorithm shape, the tree structure, the
handoff logic. The "general EEVDF" formulas in the [algorithm](
#algorithm) section *are* the implementation under weights.

A note on representation: weighted means don't divide cleanly into u64
without precision loss. A fixed-point weight scaling (e.g.
`WEIGHT_SCALE = 1024`, `w_i ∈ [WEIGHT_SCALE / 10, WEIGHT_SCALE × 10]`
for nice levels in [-10, 10]) keeps the math integer.

### Refined eligibility predicate

Useful only after one of the relaxations above (under uniform-slice +
equal-weight today, the predicate's approximation is masked by EEVDF's
"if none eligible, pick leftmost" fallback). The current
`vruntime_i ≤ avg_vruntime` is approximate; the accurate form computes
per-task `lag` at each enqueue/dequeue boundary against the V at that
moment, and amortizes the delta. Implementation cost:

- A `s64 lag` field in `sched_ticks` (signed; can be negative).
- Updates at enqueue (set from the difference between V and the
  task's vruntime), dequeue (capture and store), and at quantum start
  (refresh). The accumulation rules are mechanical but precise.
- An `avg_vruntime` formulation that tracks `sum(w_i × lag_i) /
  sum(w_i)` rather than the raw vruntime mean, so the wake-handoff's
  vruntime jolt doesn't destabilize V.

The payoff: the eligibility check becomes user-visible correct under
per-task slice differences, and the selector can drop the "pick
leftmost if none eligible" fallback (under accurate eligibility, the
condition shouldn't arise except at scheduler-genuine starvation).

### Realtime scheduling class

Tilck's longer-term direction is hard-realtime support: a separate
scheduling class for realtime tasks that strictly preempts the EEVDF
class, with tight bounds on the latency between a realtime task
becoming runnable and it actually running. That design isn't sketched
here, but the integration shape is straightforward:

- A new selection pass in `do_schedule()` *before* the worker pass
  (or possibly co-equal with it), checking for runnable realtime
  tasks.
- Realtime tasks live in their own container — probably not the EEVDF
  AVL — sorted by realtime priority or deadline depending on the
  realtime model chosen.
- The EEVDF class becomes "background" — runs only when no realtime
  task wants the CPU.

Nothing in the current EEVDF code forecloses this; the worker-first
priority pass is already the same shape.

## Glossary

- **avg_vruntime / V** — Weighted mean of vruntime across runnable +
  curr tasks. The reference virtual time used by the eligibility
  check. Under equal weights, the simple arithmetic mean.
- **deadline** — `vruntime + slice / weight`. The virtual time by
  which a task expects its current quantum to complete. Selection
  picks the eligible task with the earliest deadline.
- **eligibility** — A task is eligible at virtual time V iff
  `vruntime ≤ V` (equivalently, `lag ≥ 0`). Ineligible tasks have
  consumed more than their fair share; the scheduler skips them
  until V advances.
- **lag** — `V - vruntime`. Signed. Positive = task is owed CPU.
  Negative = task got too much. Not stored today; can be recomputed
  from vruntime and avg_vruntime on demand.
- **min_vruntime** — Monotonic high-watermark across all `RUNNING`
  vruntimes seen. Consumed by fork/wake handoffs as a floor for new
  or just-woken tasks.
- **quantum** — One contiguous span of a task being `RUNNING` between
  two `sched_start_quantum()` calls. `slice` is the budget for one
  quantum.
- **slice** — Per-quantum CPU budget. Uniform across all tasks today
  (`SCHED_LATENCY / N` clamped at `MIN_GRAN`). The basis for both the
  preemption timeout and the deadline computation.
- **subtick** — `1 / VRUNTIME_SCALE` of a real tick.
  `VRUNTIME_SCALE = 16`. The unit for `vruntime`, `slice_used`,
  `slice`, `deadline`.
- **vruntime** — Virtual runtime. Advances by `VRUNTIME_SCALE` per
  real tick of `RUNNING` time (with equal weights). The AVL tree key.
- **WAKEUP_VRUNTIME_BONUS** — `10 × VRUNTIME_SCALE`. The amount
  `wake_vruntime_handoff` floors the woken task's vruntime *below*
  `min_vruntime`, giving woken tasks a small head-start preference.
