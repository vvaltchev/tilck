/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Algorithmic correctness tests for the kernel scheduler. The goal
 * is to exercise the mathematical model deterministically and
 * synchronously, without any of the real preemption / IRQ /
 * context-switching wiring:
 *
 *   - real struct task instances (allocated via kthread_create);
 *   - real sched_account_ticks, fork_vruntime_handoff,
 *     wake_vruntime_handoff;
 *   - real selection via sched_do_select_runnable_task (exposed via
 *     STATIC + include/tilck/kernel/test/sched.h);
 *   - manual post-selection state transition done by the test
 *     fixture (switch_to_task / context_switch are NORETURN and not
 *     reachable on the aarch64 gtest host).
 *
 * No race-condition tests, no IRQ semantics, no real preemption --
 * those are selftest / shellcmd territory. The model verified here
 * is the per-tick math + selection logic, which is exactly what an
 * EEVDF swap-in will replace later.
 */

#include <gtest/gtest.h>
#include <vector>

#include "kernel_init_funcs.h"

extern "C" {
   #include <tilck/common/basic_defs.h>
   #include <tilck/common/atomics.h>
   #include <tilck/kernel/sched.h>
   #include <tilck/kernel/process_int.h>
   #include <tilck/kernel/test/sched.h>
}

using namespace std;
using namespace testing;

/*
 * kthread_create() stringifies the function name into ti->kthread_name,
 * so we need a real symbol to hand it. The body is never executed in
 * the test build -- switch_to_task / context_switch don't run -- so
 * this is just a marker.
 */
static void dummy_kthread(void *unused) { (void)unused; }

class scheduler_test : public Test {
protected:

   /*
    * Per-suite, run once. init_kmalloc_for_tests wipes the heap;
    * init_sched then creates idle_task as a real kthread (via the
    * arch stubs we converted to no-ops in tests/unit/generic_stubs.c).
    *
    * Idempotent: gtest invokes SetUpTestSuite once per suite, and
    * derived suites (e.g. scheduler_fairness_test) inherit this
    * one. A second call would wipe the heap (dangling idle_task)
    * and then re-init_sched, which fails because sched.c statics
    * (runnable_tree_root / tree_by_tid_root) still reference
    * freed memory from the first run. Guard on idle_task -- once
    * set, we keep using it across suites.
    */
   static void SetUpTestSuite() {
      if (idle_task != nullptr)
         return;
      init_kmalloc_for_tests();
      init_sched();
   }

   /*
    * After the last scheduler test, point __current back at
    * kernel_process (a constructor-initialized static struct, not
    * heap-backed) so the next test suite's init_kmalloc_for_tests()
    * doesn't leave __current dangling onto a freed idle_task.
    */
   static void TearDownTestSuite() {
      set_curr_task(kernel_process);
      atomic_store(&kernel_process->state, TASK_STATE_SLEEPING);
   }

   /*
    * Per-test setup: park __current on idle in RUNNING state so the
    * scheduler functions have a defined `curr` to work against;
    * clear need_resched and idle's timeslice; capture the current
    * min_vruntime as the test's baseline (min_vruntime grows
    * monotonically across the test process; all assertions are
    * stated relative to this baseline).
    */
   void SetUp() override {
      set_curr_task(idle_task);
      atomic_store(&idle_task->state, TASK_STATE_RUNNING);
      idle_task->ticks.timeslice = 0;
      sched_clear_need_resched();
      base_min = atomic_load(&min_vruntime);
   }

   /*
    * Take any per-test task to ZOMBIE and remove it. ZOMBIE because
    * remove_task() asserts the state. Order: clear __current's
    * pointer to a non-removed task first (back to idle), then
    * remove. Without this, __current may dangle if the running
    * task is in tasks[].
    */
   void TearDown() override {
      set_curr_task(idle_task);
      atomic_store(&idle_task->state, TASK_STATE_RUNNING);

      for (struct task *t : tasks) {
         enum task_state s = (enum task_state)atomic_load(&t->state);
         if (s != TASK_STATE_ZOMBIE)
            task_change_state(t, TASK_STATE_ZOMBIE);
         /* Mirror the kernel's death path (free_mem_for_zombie_task):
          * release the kernel_stack + I/O buffers BEFORE remove_task,
          * since free_task ASSERTs they're NULL. */
         free_common_task_allocs(t);
         remove_task(t);
      }
      tasks.clear();
   }

   /*
    * Create a kernel-thread task in TASK_STATE_RUNNABLE, with
    * vruntime placed at base_min + offset. fork_vruntime_handoff
    * (called inside allocate_new_thread) already set vruntime to
    * the current min_vruntime, so the task lands in the runnable
    * tree at that key. To place it at base_min + offset instead, we
    * pull it out of the tree (-> SLEEPING), mutate, push back.
    * Owns the task via tasks[] for TearDown.
    */
   struct task *make_task_at(u64 offset) {
      int tid = kthread_create(&dummy_kthread, 0, nullptr);
      EXPECT_GT(tid, 0);
      struct task *ti = get_task(tid);
      EXPECT_NE(ti, nullptr);

      task_change_state(ti, TASK_STATE_SLEEPING);
      atomic_store(&ti->ticks.vruntime, base_min + offset);
      task_change_state(ti, TASK_STATE_RUNNABLE);

      tasks.push_back(ti);
      return ti;
   }

   /*
    * Make `ti` the currently-running task. Mirrors switch_to_task's
    * algorithmic side effects (state transitions + timeslice reset +
    * __current update + need_resched clear) and skips the parts
    * we're explicitly not testing (FPU save, kernel-stack pointer,
    * the actual context_switch assembly).
    */
   void switch_curr_to(struct task *ti) {
      struct task *curr = get_curr_task();
      const enum task_state cs =
         (enum task_state) atomic_load(&curr->state);

      if (curr == idle_task) {
         atomic_store(&curr->state, TASK_STATE_SLEEPING);
      } else if (cs == TASK_STATE_RUNNING) {
         task_change_state(curr, TASK_STATE_RUNNABLE);
      }

      task_change_state_idempotent(ti, TASK_STATE_RUNNING);
      ti->ticks.timeslice = 0;
      set_curr_task(ti);
      sched_clear_need_resched();
   }

   u64 base_min;
   vector<struct task *> tasks;
};


/* =====================================================================
 *                     Category 1: tick accounting
 * ===================================================================== */

TEST_F(scheduler_test, vruntime_grows_per_running_tick)
{
   struct task *t = make_task_at(0);
   switch_curr_to(t);

   const u64 v0 = atomic_load(&t->ticks.vruntime);
   sched_account_ticks();
   const u64 v1 = atomic_load(&t->ticks.vruntime);

   EXPECT_EQ(v1 - v0, (u64) SCHED_TEST_VRUNTIME_SCALE);
}

TEST_F(scheduler_test, vruntime_does_not_grow_for_idle)
{
   /* __current is idle_task in SetUp. Per the algorithm,
    * sched_account_ticks gates the vruntime increment on
    * (is_running && curr != idle_task). */
   const u64 v0 = atomic_load(&idle_task->ticks.vruntime);
   sched_account_ticks();
   const u64 v1 = atomic_load(&idle_task->ticks.vruntime);
   EXPECT_EQ(v1, v0);
}

TEST_F(scheduler_test, total_ticks_counts_idle_too)
{
   /* `total` is a different counter from vruntime; it counts every
    * tick the task spent as curr, including idle ticks. */
   const u64 t0 = idle_task->ticks.total;
   sched_account_ticks();
   sched_account_ticks();
   sched_account_ticks();
   EXPECT_EQ(idle_task->ticks.total, t0 + 3);
}

TEST_F(scheduler_test, min_vruntime_tracks_running_task)
{
   struct task *t = make_task_at(0);
   switch_curr_to(t);

   for (int i = 0; i < 3; i++)
      sched_account_ticks();

   /* After 3 ticks at +VRUNTIME_SCALE each, t->vruntime advanced by
    * 3*SCALE and min_vruntime should mirror that growth (it's the
    * monotonic high-watermark of curr->vruntime). */
   EXPECT_EQ(atomic_load(&t->ticks.vruntime),
             base_min + 3 * SCHED_TEST_VRUNTIME_SCALE);
   EXPECT_EQ(atomic_load(&min_vruntime),
             atomic_load(&t->ticks.vruntime));
}

TEST_F(scheduler_test, min_vruntime_is_monotonic)
{
   /* Even if a task with a lower vruntime later becomes curr, the
    * min_vruntime watermark must not decrease. */
   struct task *fast = make_task_at(100);
   struct task *slow = make_task_at(0);

   switch_curr_to(fast);
   sched_account_ticks();
   const u64 hi = atomic_load(&min_vruntime);
   EXPECT_GE(hi, base_min + 100);

   switch_curr_to(slow);
   sched_account_ticks();
   EXPECT_GE(atomic_load(&min_vruntime), hi);
}


/* =====================================================================
 *                  Category 2: dynamic timeslice / timeout
 * ===================================================================== */

TEST_F(scheduler_test, timeslice_ends_at_dynamic_slice)
{
   /*
    * Dynamic slice: MAX(SCHED_LATENCY * SCALE / N, MIN_GRAN * SCALE)
    * where N = runnable_tasks_count + 1. With 3 RUNNABLE peers and
    * curr (running), N = 4. The full slice in subticks is then:
    *
    *    slice = MAX(SCHED_LATENCY_TICKS * SCALE / 4,
    *                MIN_GRANULARITY_TICKS * SCALE)
    *
    * sched_account_ticks bumps timeslice by SCALE per call, so
    * need_resched should fire after exactly (slice / SCALE) calls
    * (= ceil but the math is exact when the inputs are integer
    * multiples).
    */
   make_task_at(0);   /* peer 1 */
   make_task_at(0);   /* peer 2 */
   make_task_at(0);   /* peer 3 */
   struct task *curr = make_task_at(0);
   switch_curr_to(curr);

   const u32 nr_running = 4;
   const u32 slice_subticks =
      MAX(SCHED_LATENCY_TICKS * SCHED_TEST_VRUNTIME_SCALE / nr_running,
          (u32) MIN_GRANULARITY_TICKS * SCHED_TEST_VRUNTIME_SCALE);
   const u32 slice_ticks = slice_subticks / SCHED_TEST_VRUNTIME_SCALE;

   for (u32 i = 1; i < slice_ticks; i++) {
      sched_account_ticks();
      EXPECT_FALSE(need_reschedule())
         << "need_resched fired too early at tick " << i
         << " (expected at " << slice_ticks << ")";
   }
   sched_account_ticks();
   EXPECT_TRUE(need_reschedule());
}

TEST_F(scheduler_test, timeslice_clamps_at_min_granularity)
{
   /*
    * With many runnable tasks, SCHED_LATENCY/N drops below
    * MIN_GRAN. The slice must clamp at MIN_GRAN ticks.
    */
   const int n_peers =
      SCHED_LATENCY_TICKS / MIN_GRANULARITY_TICKS + 8;

   for (int i = 0; i < n_peers; i++)
      make_task_at(0);

   struct task *curr = make_task_at(0);
   switch_curr_to(curr);

   for (u32 i = 1; i < (u32) MIN_GRANULARITY_TICKS; i++) {
      sched_account_ticks();
      EXPECT_FALSE(need_reschedule())
         << "need_resched fired too early at tick " << i;
   }
   sched_account_ticks();
   EXPECT_TRUE(need_reschedule());
}

TEST_F(scheduler_test, non_running_curr_resched_fires_immediately)
{
   /*
    * If curr is not RUNNING (e.g. SLEEPING after wait_on_cond) when
    * the timer IRQ fires, sched_account_ticks must set need_resched
    * regardless of the slice budget -- the "!is_running || timeout"
    * disjunction in the algorithm.
    */
   struct task *t = make_task_at(0);
   switch_curr_to(t);
   /* __current is t, RUNNING. Move it to SLEEPING but keep
    * __current pointing at it (the situation an IRQ catches when a
    * task has just enter_sleep_wait_state'd). */
   task_change_state(t, TASK_STATE_SLEEPING);
   sched_clear_need_resched();

   sched_account_ticks();
   EXPECT_TRUE(need_reschedule());
}


/* =====================================================================
 *                     Category 3: selection
 * ===================================================================== */

TEST_F(scheduler_test, select_leftmost_by_vruntime)
{
   /* curr = idle (set in SetUp). Selection should pick the task
    * with the smallest vruntime out of the runnable tree. */
   make_task_at(30);
   struct task *t10 = make_task_at(10);
   make_task_at(20);

   struct task *selected = sched_do_select_runnable_task(
      (enum task_state) atomic_load(&idle_task->state), true);

   EXPECT_EQ(selected, t10);
}

TEST_F(scheduler_test, select_tiebreaks_by_tid)
{
   /* Equal vruntime ties broken by tid (smaller wins -- bintree
    * leftmost descent visits the smaller tiebreaker first). */
   struct task *a = make_task_at(50);
   struct task *b = make_task_at(50);
   EXPECT_LT(a->tid, b->tid);

   struct task *selected = sched_do_select_runnable_task(
      (enum task_state) atomic_load(&idle_task->state), true);

   EXPECT_EQ(selected, a);
}

TEST_F(scheduler_test, select_keeps_curr_when_tree_empty_and_running)
{
   /* No RUNNABLE peers; curr is still happily RUNNING. Selection
    * keeps it (avoids gratuitous churn through idle and back). */
   struct task *curr = make_task_at(0);
   switch_curr_to(curr);
   /* Now: runnable tree is empty (curr is in RUNNING state, not in
    * the tree). */
   struct task *selected = sched_do_select_runnable_task(
      TASK_STATE_RUNNING, true);
   EXPECT_EQ(selected, curr);
}

TEST_F(scheduler_test, select_returns_null_when_tree_empty_and_not_running)
{
   /* Curr is not RUNNING (e.g. went to SLEEPING) and no other
    * tasks are runnable. Selection returns NULL; do_schedule()
    * would then fall back to idle. */
   struct task *selected = sched_do_select_runnable_task(
      TASK_STATE_SLEEPING, true);
   EXPECT_EQ(selected, nullptr);
}

TEST_F(scheduler_test, select_keeps_curr_when_curr_is_leftmost_and_not_resched)
{
   /*
    * Curr is RUNNING with the lowest vruntime; tree has only a
    * higher-vruntime task. With resched=false (need_resched not
    * set), selection must keep curr -- the "don't preempt curr if
    * its vruntime is lower than the leftmost" path.
    */
   make_task_at(100);
   struct task *curr = make_task_at(0);
   switch_curr_to(curr);

   struct task *selected = sched_do_select_runnable_task(
      TASK_STATE_RUNNING, false);

   EXPECT_EQ(selected, curr);
}

TEST_F(scheduler_test, select_preempts_curr_when_leftmost_lower_and_not_resched)
{
   /*
    * Curr is RUNNING but with a higher vruntime than the leftmost.
    * Even with resched=false, leftmost wins -- the "don't preempt"
    * path only protects curr when curr is genuinely leftmost.
    */
   struct task *t_low = make_task_at(0);
   struct task *curr = make_task_at(100);
   switch_curr_to(curr);

   struct task *selected = sched_do_select_runnable_task(
      TASK_STATE_RUNNING, false);

   EXPECT_EQ(selected, t_low);
}

TEST_F(scheduler_test, select_preempts_curr_when_resched_true)
{
   /*
    * With resched=true (need_resched was set, e.g. by the slice
    * timeout), the leftmost wins unconditionally -- the "don't
    * preempt" guard does not fire when resched is true.
    */
   struct task *t_low = make_task_at(0);
   struct task *curr = make_task_at(100);
   switch_curr_to(curr);

   struct task *selected = sched_do_select_runnable_task(
      TASK_STATE_RUNNING, true);

   EXPECT_EQ(selected, t_low);
}


/* =====================================================================
 *                  Category 4: fork / wake handoffs
 * ===================================================================== */

TEST_F(scheduler_test, fork_handoff_sets_vruntime_to_min)
{
   /* Bump min_vruntime by running a task for a few ticks. */
   struct task *t = make_task_at(0);
   switch_curr_to(t);
   for (int i = 0; i < 4; i++)
      sched_account_ticks();

   const u64 expected = atomic_load(&min_vruntime);

   /* fork_vruntime_handoff overwrites vruntime to min_vruntime
    * exactly (no bonus -- fresh forks start at the leading edge,
    * not below it). */
   struct task scratch = {};
   fork_vruntime_handoff(&scratch);
   EXPECT_EQ(atomic_load(&scratch.ticks.vruntime), expected);
}

TEST_F(scheduler_test, wake_handoff_raises_to_floor)
{
   /*
    * After min_vruntime grows past WAKEUP_VRUNTIME_BONUS, a sleeper
    * waking up with stale-low vruntime should be raised to
    * min_vruntime - BONUS.
    */
   struct task *t = make_task_at(0);
   switch_curr_to(t);

   /* Tick until min_vruntime is comfortably above BONUS. */
   const int needed_ticks =
      (SCHED_TEST_WAKEUP_VRUNTIME_BONUS / SCHED_TEST_VRUNTIME_SCALE) + 5;
   for (int i = 0; i < needed_ticks; i++)
      sched_account_ticks();

   const u64 m = atomic_load(&min_vruntime);
   ASSERT_GT(m, (u64) SCHED_TEST_WAKEUP_VRUNTIME_BONUS);
   const u64 floor = m - SCHED_TEST_WAKEUP_VRUNTIME_BONUS;

   /* Build a sleeper at vruntime = 0 (well below floor). */
   struct task *sleeper = make_task_at(0);
   task_change_state(sleeper, TASK_STATE_SLEEPING);
   atomic_store(&sleeper->ticks.vruntime, 0);

   wake_vruntime_handoff(sleeper);
   EXPECT_EQ(atomic_load(&sleeper->ticks.vruntime), floor);
}

TEST_F(scheduler_test, wake_handoff_does_not_lower_a_high_vruntime)
{
   /* A sleeper whose vruntime is already above the floor must not
    * be touched -- wake handoff is a monotonic raise, never a
    * decrease. */
   struct task *t = make_task_at(0);
   switch_curr_to(t);
   for (int i = 0; i < 8; i++)
      sched_account_ticks();

   struct task *sleeper = make_task_at(0);
   task_change_state(sleeper, TASK_STATE_SLEEPING);
   const u64 hi = atomic_load(&min_vruntime) + 100;
   atomic_store(&sleeper->ticks.vruntime, hi);

   wake_vruntime_handoff(sleeper);
   EXPECT_EQ(atomic_load(&sleeper->ticks.vruntime), hi);
}

TEST_F(scheduler_test, wake_handoff_noop_when_state_not_sleeping)
{
   /* Double-wake race: wake_up() called on an already-RUNNABLE task
    * (because a peer wake won the wait_obj reset first). The handoff
    * must be a no-op -- the algorithmic invariant is "we don't
    * mutate vruntime of a task already in the runnable tree". */
   struct task *t = make_task_at(0);
   switch_curr_to(t);
   for (int i = 0; i < 4; i++)
      sched_account_ticks();

   struct task *runnable = make_task_at(0);   /* state == RUNNABLE */
   const u64 before = atomic_load(&runnable->ticks.vruntime);

   wake_vruntime_handoff(runnable);
   EXPECT_EQ(atomic_load(&runnable->ticks.vruntime), before);
}

TEST_F(scheduler_test, wake_handoff_underflow_guarded_at_zero)
{
   /*
    * Early-boot case: min_vruntime < WAKEUP_VRUNTIME_BONUS.
    * Naively, floor = min_vruntime - BONUS would underflow on u64.
    * The algorithm guards: if (current_min > BONUS) floor =
    * current_min - BONUS else floor = 0. Force the < BONUS state
    * by writing min_vruntime directly (this is exactly why it's
    * surfaced for tests).
    */
   const u64 saved_min = atomic_load(&min_vruntime);
   atomic_store(&min_vruntime,
                (u64)(SCHED_TEST_WAKEUP_VRUNTIME_BONUS / 2));

   struct task *sleeper = make_task_at(0);
   task_change_state(sleeper, TASK_STATE_SLEEPING);
   atomic_store(&sleeper->ticks.vruntime, 0);

   wake_vruntime_handoff(sleeper);

   /* Floor must be 0, NOT a giant wraparound value. */
   EXPECT_EQ(atomic_load(&sleeper->ticks.vruntime), 0u);

   /* Restore min_vruntime so the next test isn't surprised. */
   atomic_store(&min_vruntime, saved_min);
}


/* =====================================================================
 *           Category 5: workload-driven fairness simulator
 *
 * Drives the real scheduler through N synthetic ticks with a vector
 * of scheduled events. Per-tick:
 *
 *   - apply any sim_event whose .tick matches (sleep / wake);
 *   - create any task whose fork_at_tick matches (real
 *     kthread_create -> fork_vruntime_handoff -> add_task);
 *   - call sched_account_ticks();
 *   - if need_reschedule, run sched_do_select_runnable_task and
 *     simulate the switch via the fixture helper;
 *   - record per-tick "who ran" + per-task tick counts.
 *
 * The same harness will validate EEVDF's selection logic when it
 * replaces the leftmost-pick: only the kernel-side selector
 * changes; this driver and the assertions stay the same.
 * ===================================================================== */

enum sim_action {
   SIM_SLEEP,    /* task transitions RUNNING/RUNNABLE -> SLEEPING        */
   SIM_WAKE,     /* task transitions SLEEPING -> RUNNABLE w/ handoff     */
};

struct sim_event {
   u64 tick;
   int tid_idx;          /* index into sim_workload::tasks         */
   enum sim_action act;
};

struct sim_task_spec {
   /*
    * Tick at which this task is created. 0 means "exists from
    * tick 0, RUNNABLE". >0 means kthread_create runs at that
    * tick, exercising fork_vruntime_handoff against the
    * accumulated min_vruntime.
    */
   u64 fork_at_tick;
};

struct sim_workload {
   std::vector<sim_task_spec> tasks;
   std::vector<sim_event> events;
   u64 n_ticks;
};

struct sim_result {
   std::vector<u64> cpu_ticks;          /* per-task cumulative ticks   */
   std::vector<int> per_tick_curr;      /* [n_ticks], -1 = idle        */
   u64 idle_ticks;
};

class scheduler_fairness_test : public scheduler_test {
protected:
   /*
    * Create a kernel thread the natural way: kthread_create runs
    * fork_vruntime_handoff against the CURRENT min_vruntime and
    * add_task inserts into the runnable tree. Used for both the
    * initial pool of tasks and the fork-at-tick-K case (the only
    * difference is timing, not mechanism).
    */
   struct task *make_fork_task() {
      int tid = kthread_create(&dummy_kthread, 0, nullptr);
      EXPECT_GT(tid, 0);
      struct task *ti = get_task(tid);
      EXPECT_NE(ti, nullptr);
      tasks.push_back(ti);
      return ti;
   }

   /*
    * Apply one event. SLEEP transitions the task out of the
    * runnable tree (the next sched_account_ticks will see
    * !is_running on curr and set need_resched). WAKE mirrors
    * kernel/wobj.c:wake_up: handoff first, then state change to
    * RUNNABLE (insert into tree at the new key).
    */
   void apply_event(const sim_event &ev,
                    std::vector<struct task *> &task_ptrs)
   {
      struct task *t = task_ptrs[ev.tid_idx];
      ASSERT_NE(t, nullptr);

      switch (ev.act) {

         case SIM_SLEEP:
            task_change_state(t, TASK_STATE_SLEEPING);
            break;

         case SIM_WAKE:
            wake_vruntime_handoff(t);
            task_change_state(t, TASK_STATE_RUNNABLE);
            break;
      }
   }

   /*
    * Mimic kernel/sched.c:do_schedule's tail: if need_resched is
    * set, pick the next runnable task and switch. Falls back to
    * idle_task when the tree is empty, matching do_schedule's
    * `if (!selected) selected = idle_task;` line.
    */
   void advance_one_tick(sim_result &r, u64 t,
                         const std::vector<struct task *> &task_ptrs)
   {
      sched_account_ticks();

      if (need_reschedule()) {
         struct task *curr = get_curr_task();
         const enum task_state cs =
            (enum task_state) atomic_load(&curr->state);
         struct task *next =
            sched_do_select_runnable_task(cs, true);

         if (!next)
            next = idle_task;

         if (next != curr)
            switch_curr_to(next);
         else
            sched_clear_need_resched();
      }

      struct task *curr = get_curr_task();
      if (curr == idle_task) {
         r.idle_ticks++;
         r.per_tick_curr[t] = -1;
      } else {
         for (size_t i = 0; i < task_ptrs.size(); i++) {
            if (task_ptrs[i] == curr) {
               r.cpu_ticks[i]++;
               r.per_tick_curr[t] = (int)i;
               break;
            }
         }
      }
   }

   sim_result run_workload(const sim_workload &w) {

      /*
       * Strict-monotonic tick precheck. Two events at the same
       * tick are explicitly disallowed: it's almost always a
       * test bug, and forbidding it keeps the per-tick semantics
       * unambiguous.
       */
      for (size_t i = 1; i < w.events.size(); i++) {
         EXPECT_LT(w.events[i - 1].tick, w.events[i].tick)
            << "sim_workload events must be strictly tick-monotonic"
               " (event " << i - 1 << " and " << i << ")";
      }

      sim_result r;
      r.cpu_ticks.assign(w.tasks.size(), 0);
      r.per_tick_curr.assign(w.n_ticks, -1);
      r.idle_ticks = 0;

      std::vector<struct task *> task_ptrs(w.tasks.size(), nullptr);

      /* Create the tasks scheduled for tick 0. */
      for (size_t i = 0; i < w.tasks.size(); i++) {
         if (w.tasks[i].fork_at_tick == 0)
            task_ptrs[i] = make_fork_task();
      }

      size_t event_idx = 0;
      for (u64 t = 0; t < w.n_ticks; t++) {

         /* Forks scheduled for this tick (after tick 0). */
         if (t > 0) {
            for (size_t i = 0; i < w.tasks.size(); i++) {
               if (w.tasks[i].fork_at_tick == t && !task_ptrs[i])
                  task_ptrs[i] = make_fork_task();
            }
         }

         /* Events scheduled for this tick. */
         while (event_idx < w.events.size() &&
                w.events[event_idx].tick == t)
         {
            apply_event(w.events[event_idx], task_ptrs);
            event_idx++;
         }

         advance_one_tick(r, t, task_ptrs);
      }

      return r;
   }

   /* CPU ticks for `tid_idx` in the half-open window [start, end). */
   static u64 cpu_in_window(const sim_result &r, int tid_idx,
                            u64 start, u64 end)
   {
      u64 count = 0;
      const u64 cap = std::min(end, (u64) r.per_tick_curr.size());
      for (u64 t = start; t < cap; t++) {
         if (r.per_tick_curr[t] == tid_idx)
            count++;
      }
      return count;
   }
};

TEST_F(scheduler_fairness_test, equal_weight_all_runnable)
{
   /*
    * N equal-weight tasks runnable for the whole window. Each
    * should get ~1/N of the CPU. Window is large enough
    * (2000 ticks at 250 Hz = 8 s guest time) for the slice-
    * discretization noise to average out under a 5% tolerance.
    */
   sim_workload w;
   w.n_ticks = 2000;
   w.tasks.resize(4);

   sim_result r = run_workload(w);

   for (size_t i = 0; i < w.tasks.size(); i++) {
      const double share = (double) r.cpu_ticks[i] / (double) w.n_ticks;
      EXPECT_NEAR(share, 0.25, 0.05)
         << "task " << i << " got " << r.cpu_ticks[i]
         << " ticks (share " << share << ")";
   }

   /*
    * Idle should barely run -- only the initial slice the boot
    * sets curr=idle for before need_resched fires. Cap at 5%.
    */
   EXPECT_LE(r.idle_ticks, w.n_ticks / 20);
}

TEST_F(scheduler_fairness_test, post_wake_sleeper_does_not_dominate)
{
   /*
    * Task 0 sleeps for the first half of the run, wakes at the
    * midpoint. Without the wake handoff, its vruntime would be
    * stale-low when it wakes and it would dominate the second
    * half (CFS's pre-handoff pathology). With the handoff at
    * `min_vruntime - WAKEUP_VRUNTIME_BONUS`, it gets a one-slice
    * head start and then falls back into the 1/N rotation.
    *
    * Post-wake window [1000, 2000), 3 runnable tasks, expected
    * share ~33%. Tolerate up to 50% to absorb the one-slice
    * BONUS head start and slice-discretization noise.
    */
   sim_workload w;
   w.n_ticks = 2000;
   w.tasks.resize(3);
   w.events = {
      { 1,    0, SIM_SLEEP },
      { 1000, 0, SIM_WAKE  },
   };

   sim_result r = run_workload(w);

   const u64 post = cpu_in_window(r, 0, 1000, w.n_ticks);
   const double share = (double) post / (double) (w.n_ticks - 1000);
   EXPECT_LE(share, 0.50)
      << "task 0 dominated post-wake (share=" << share << ")";
   EXPECT_GE(share, 0.10)
      << "task 0 starved post-wake (share=" << share << ")";
}

TEST_F(scheduler_fairness_test, fresh_fork_does_not_leapfrog)
{
   /*
    * Two tasks run for the first half accumulating vruntime; a
    * third task is forked at the midpoint via real
    * kthread_create -> fork_vruntime_handoff. The handoff lands
    * its vruntime at the current min_vruntime, so it joins the
    * 1/N rotation rather than leapfrogging the existing tasks
    * (which would happen if it started at vruntime=0 while the
    * existing tasks were already deep into the high hundreds of
    * subticks).
    */
   sim_workload w;
   w.n_ticks = 2000;
   w.tasks.resize(3);
   w.tasks[2].fork_at_tick = 1000;

   sim_result r = run_workload(w);

   const u64 post = cpu_in_window(r, 2, 1000, w.n_ticks);
   const double share = (double) post / (double) (w.n_ticks - 1000);
   EXPECT_LE(share, 0.50)
      << "freshly-forked task dominated (share=" << share << ")";
   EXPECT_GE(share, 0.10)
      << "freshly-forked task starved (share=" << share << ")";
}
