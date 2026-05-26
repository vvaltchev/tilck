/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/test/sched.h>

/* Shared global variables */
struct task *__current;
atomic_int_t __disable_preempt = { .v = 1 }; /* see docs/atomics.md */
atomic_int_t __need_resched;                 /* see docs/atomics.md */

struct task *kernel_process;
struct process *kernel_process_pi;

/*
 * Runnable tasks tree.
 *
 * AVL tree keyed by (vruntime, tid). Selection takes the leftmost
 * entry -- CFS-style. tid is the tiebreaker since bintree_insert()
 * requires unique keys; tids are unique system-wide, so the
 * composite key is always unique.
 *
 * Invariant: a task's vruntime is never modified while it sits in
 * this tree. The only writer of vruntime in IRQ context is
 * sched_account_ticks() which mutates curr (which is RUNNING, not in
 * the tree). The wake/fork handoffs (wake_vruntime_handoff,
 * fork_vruntime_handoff) write vruntime BEFORE the task enters the
 * RUNNABLE state and gets inserted. As long as this invariant holds,
 * ordering-by-vruntime is stable.
 *
 * idle_task is intentionally NOT in this tree -- it lives in its own
 * fallback slot consulted by do_schedule() when the tree is empty.
 * Keeping idle out of the tree avoids a class of bugs where idle
 * would have to be filtered at every walk site and would risk
 * double-insertion on the curr-RUNNING -> curr-RUNNABLE transition.
 */
struct task *runnable_tree_root;

/* Static variables */
static struct task *tree_by_tid_root;
static u64 idle_ticks;
static atomic_int_t runnable_tasks_count;    /* see docs/atomics.md */
static int current_max_pid = -1;
static int current_max_kernel_tid = -1;
struct task *idle_task;

/*
 * Monotonic high-watermark vruntime. CFS-style baseline consumed by
 * `wake_vruntime_handoff()` as the floor when bringing a long-sleeper's
 * vruntime back up at wake-up. Updated in sched_account_ticks(); never
 * decreases.
 */
STATIC atomic_u64_t min_vruntime;

/*
 * Sum of vruntime over tasks currently in the runnable tree.
 * Incrementally maintained at insert/remove. Combined with curr's
 * vruntime in sched_compute_avg_vruntime() to give the EEVDF
 * "virtual time" V used by the eligibility check.
 *
 * Equal-weight collapse: with all weights = 1 this is a plain sum.
 * General EEVDF would track sum(w_i * v_i) plus sum(w_i) separately.
 * See docs/scheduler.md.
 */
STATIC atomic_u64_t sum_vruntime_in_tree;

/*
 * Sub-tick precision factor. vruntime, slice_used and slice are
 * stored in "subticks" -- 1 real tick == VRUNTIME_SCALE subticks
 * -- so the integer math in sched_account_ticks() keeps useful
 * resolution when SCHED_LATENCY_TICKS / nr_running would truncate
 * hard (e.g. 20/3 = 6 instead of 6.67 in raw ticks, but 320/3 = 106
 * subticks = 6.625 ticks with VRUNTIME_SCALE = 16).
 *
 * 16 was picked as the sweet spot: 4 bits of sub-tick precision is
 * enough to keep slice ratios sharp at the runnable counts Tilck
 * actually hits, and the multiply lowers to a single shift. u64
 * vruntime overflows at ~2^60 subticks, hundreds of years at
 * KRN_TIMER_HZ=250, so the larger numbers don't shorten the
 * overflow horizon in any practical sense.
 *
 * Power-of-two so the compiler can fold *VRUNTIME_SCALE / SCALE
 * into shifts; keep it that way if you ever tune it.
 */
#define VRUNTIME_SCALE           16

/*
 * How far below `min_vruntime` we let a woken task's vruntime sit.
 * A long-sleeping task that wakes with stale-low vruntime would
 * otherwise dominate the CPU until it caught up; we raise it to
 * `(min_vruntime - WAKEUP_VRUNTIME_BONUS)` with an underflow guard at
 * 0 so it gets a small head start over the leading edge instead.
 *
 * Expressed in subticks (see VRUNTIME_SCALE). 10 ticks' worth of
 * head start = 10 * VRUNTIME_SCALE = 160 subticks; at KRN_TIMER_HZ
 * = 250 that's one default time slice (40 ms). Tunable.
 */
#define WAKEUP_VRUNTIME_BONUS    (10 * VRUNTIME_SCALE)

static ALWAYS_INLINE int get_runnable_tasks_count(void)
{
   return atomic_load(&runnable_tasks_count);
}

/*
 * Comparator for `runnable_tree_root`. Key: (vruntime, tid).
 * vruntime is the primary sort key; tid is the tiebreaker so the
 * composite is unique across the system (bintree_insert requires
 * unique keys). The same comparator is used for both insert and
 * remove -- bintree_remove passes the task pointer as `value_ptr`,
 * matching this signature.
 */
static long sched_runnable_cmp(const void *a, const void *b)
{
   /*
    * The cmpfun_ptr signature is `const void *` both arguments;
    * cast back to non-const struct task * so atomic_load() can be
    * called on the vruntime field (the dispatch macro doesn't have
    * a const overload). atomic_load is semantically read-only;
    * dropping the qualifier is safe.
    */
   struct task *t1 = (struct task *)a;
   struct task *t2 = (struct task *)b;

   const u64 v1 = atomic_load(&t1->ticks.vruntime);
   const u64 v2 = atomic_load(&t2->ticks.vruntime);

   if (v1 != v2)
      return v1 < v2 ? -1 : 1;

   return (long)t1->tid - (long)t2->tid;
}

const char *const task_state_str[6] = {
   [TASK_STATE_INVALID]  = "invalid",
   [TASK_STATE_RUNNABLE] = "runnable",
   [TASK_STATE_RUNNING]  = "running",
   [TASK_STATE_SLEEPING] = "sleeping",
   [TASK_STATE_ZOMBIE]   = "zombie",
   [TASK_STATE_STOPPED]  = "stopped",
};

void enable_preemption(void)
{
   int oldval = atomic_fetch_sub(&__disable_preempt, 1);

   ASSERT(oldval > 0);

   if (KRN_RESCHED_ENABLE_PREEMPT) {
      if (oldval == 1 && need_reschedule() && are_interrupts_enabled())
         schedule();
   }
}

bool save_regs_and_schedule(bool skip_disable_preempt)
{
   /* Private declaraction of the low-level yield function */
   extern bool asm_save_regs_and_schedule(void *);

   bool context_switch;

   if (skip_disable_preempt) {
      ASSERT(get_preempt_disable_count() == 1);
   } else {
      ASSERT(get_preempt_disable_count() == 0);
      disable_preemption();
   }

   context_switch = asm_save_regs_and_schedule(
      __builtin_extract_return_addr(__builtin_return_address(0))
   );

   if (UNLIKELY(!context_switch))
      enable_preemption_nosched();

   return context_switch;
}

int
get_traced_tasks_count(void)
{
   struct bintree_walk_ctx ctx;
   struct task *ti;
   int count = 0;

   disable_preemption();
   {
      bintree_in_order_visit_start(&ctx,
                                   tree_by_tid_root,
                                   struct task,
                                   tree_by_tid_node,
                                   false);

      while ((ti = bintree_in_order_visit_next(&ctx))) {
         if (ti->traced)
            count++;
      }
   }
   enable_preemption();
   return count;
}

int sched_count_proc_in_group(int pgid)
{
   struct bintree_walk_ctx ctx;
   struct task *ti;
   int count = 0;

   disable_preemption();
   {
      bintree_in_order_visit_start(&ctx,
                                   tree_by_tid_root,
                                   struct task,
                                   tree_by_tid_node,
                                   false);

      while ((ti = bintree_in_order_visit_next(&ctx))) {

         if (ti->pi->pgid == pgid)
            count++;
      }
   }
   enable_preemption();
   return count;
}

int sched_get_session_of_group(int pgid)
{
   struct bintree_walk_ctx ctx;
   struct task *ti;
   int sid = -ESRCH;

   disable_preemption();
   {
      bintree_in_order_visit_start(&ctx,
                                   tree_by_tid_root,
                                   struct task,
                                   tree_by_tid_node,
                                   false);

      while ((ti = bintree_in_order_visit_next(&ctx))) {

         if (ti->pi->pgid == pgid) {
            sid = ti->pi->sid;
            break;
         }
      }
   }
   enable_preemption();
   return sid;
}

int get_curr_tid(void)
{
   struct task *c = get_curr_task();
   return c ? c->tid : 0;
}

int get_curr_pid(void)
{
   struct task *c = get_curr_task();
   return c ? c->pi->pid : 0;
}

struct create_pid_visit_ctx {

   bool kernel_tid;
   bool lowest_avail_invalid;
   int id_off;
   int lowest_available;
   int lowest_after_current_max;
};

static inline int inc_candicate_if_matches(int *cand, int id, int val)
{
   if (*cand != val)
      return 0; /* Everything is OK */

   /* The lowest_<something> candicate cannot be > id */
   ASSERT(*cand <= val);

   /*
    * We've found a case where the candicate is equal to current task's pgid/sid
    * and that might be a problem.
    *
    * Two cases:
    *
    *    1) Candidate == id. This means: candidate == pgid/sid == id.
    *       That's fine: we just hit a group/session leader. By default we'll
    *       skip this candiate (see the impl. below).
    *
    *    2) Candidate < id. This means that the pgid/sid is < id and we didn't
    *       hit it before (note: we're doing an in-order visit). For example:
    *
    *    pid | sid | what
    *    ----+-----+-----------
    *     1  |  1  | init
    *     2  |  1  | ash
    *     8  |  3  | prog1
    *
    *    lowest_available = 3   [our candidate]
    *
    *    We don't want this to happen because 3 is the ID of a session, with
    *    a dead leader. In order to handle this case, we have to increment
    *    lowest_available and restart the whole iteration. We must end up with
    *    lowest_available = 4. Actually, if lowest_after_current_max is a good
    *    value, we're going to use it without restarting the iteration.
    */

   if (*cand == id)
      return 0; /* We just hit a group/session leader. That's fine */

   (*cand)++;
   return 1;
}

static int create_new_pid_visit_cb(void *obj, void *arg)
{
   struct task *ti = obj;
   struct create_pid_visit_ctx *ctx = arg;
   const int tid = ti->tid - ctx->id_off;

   if (ctx->kernel_tid) {

      if (!is_kernel_thread(ti))
         return 0; /* skip non-kernel tasks */

      ASSERT(tid >= 0);


   } else {

      if (!is_main_thread(ti))
         return 0; /* skip threads */

      ASSERT(tid >= 0);

      if (tid < ctx->lowest_available)
         return 0;

      /*
       * Both ctx->lowest_available and ctx->lowest_after_current_max are
       * candidates for the next tid. If one of them gets equal to a sid
       * without a session leader process or to a pgid without a group leader,
       * the new process could accidentally become leader of a group/session:
       * we have to ensure that could never happen.
       */

      const int pgid = ti->pi->pgid;
      const int sid = ti->pi->sid;
      const int should_restart =
         inc_candicate_if_matches(&ctx->lowest_available, tid, pgid) +
         inc_candicate_if_matches(&ctx->lowest_available, tid, sid) +
         inc_candicate_if_matches(&ctx->lowest_after_current_max, tid, pgid) +
         inc_candicate_if_matches(&ctx->lowest_after_current_max, tid, sid);

      if (should_restart) {
         ctx->lowest_avail_invalid = true;
      }
   }


   /*
    * Algorithm: we start with lowest_available (L) == 0. When we hit
    * tid == L, that means L is not really the lowest, therefore, we guess
    * the right value of L is L + 1. The first time tid skips one, for example
    * jumping from 3 to 5, the value of L set by the iteration with tid == 3,
    * will stuck. That value will be clearly 4.
    */

   if (ctx->lowest_available == tid)
      ctx->lowest_available++;

   /*
    * For lowest_after_current_max (A) the logic is similar.
    * We start with A = current_max_pid + 1. The first time A is == tid, will
    * be when tid is current_max_pid + 1. We continue to update A, until the
    * first hole is found. In case tid never reaches current_max_pid + 1, `A`
    * will be just be current_max_pid + 1, as expected.
    */

   if (ctx->lowest_after_current_max == tid)
      ctx->lowest_after_current_max++;

   return 0;
}

static int
create_id_common(struct create_pid_visit_ctx *ctx, int max_id)
{
   int r;

   do {

      ctx->lowest_avail_invalid = false;
      iterate_over_tasks(&create_new_pid_visit_cb, ctx);

      r = ctx->lowest_after_current_max <= max_id
            ? ctx->lowest_after_current_max
            : ctx->lowest_available <= max_id
               ? ctx->lowest_available
               : -1;

      /*
       * Typically, ctx->lowest_avail_invalid is not a problem because the new
       * id will be ctx->lowest_after_current_max. But, in the case we've
       * finished the IDs, we have to pick the absolutely lowest ID available,
       * which might be invalid we it matched with a process' pgid/sid. This
       * can happen only in case of orphaned sessions/process groups.
       *
       * NOTE: in case we're going to repeat the loop, lowest_avail_invalid is
       * reset, but all the rest of the context is unchanged: that's because
       * ctx->lowest_available has actually a meaningful value: the lowest ID
       * after pgid/sid we hit. It doesn't mean we can use this value, but it
       * means we cannot pick consider IDs smaller than it.
       */

   } while (r == ctx->lowest_available && ctx->lowest_avail_invalid);

   return r;
}

int create_new_pid(void)
{
   struct create_pid_visit_ctx ctx;
   int r;

   ASSERT(!is_preemption_enabled());

   ctx = (struct create_pid_visit_ctx) {
      .kernel_tid = false,
      .id_off = 0,
      .lowest_available = 0,
      .lowest_after_current_max = current_max_pid + 1,
   };

   r = create_id_common(&ctx, MAX_PID);

   if (r >= 0)
      current_max_pid = r;

   return r;
}

int create_new_kernel_tid(void)
{
   struct create_pid_visit_ctx ctx;
   int r;

   ASSERT(!is_preemption_enabled());

   ctx = (struct create_pid_visit_ctx) {
      .kernel_tid = true,
      .id_off = KERNEL_TID_START,
      .lowest_available = 0,
      .lowest_after_current_max = current_max_kernel_tid + 1,
   };

   r = create_id_common(&ctx, KERNEL_MAX_TID);

   if (r >= 0) {
      current_max_kernel_tid = r;
      return r + KERNEL_TID_START;
   }

   return -1;
}

int iterate_over_tasks(bintree_visit_cb func, void *arg)
{
   ASSERT(!is_preemption_enabled());

   return bintree_in_order_visit(tree_by_tid_root,
                                 func,
                                 arg,
                                 struct task,
                                 tree_by_tid_node);
}

static void idle(void *unused)
{
   while (true) {

      ASSERT(is_preemption_enabled());

      idle_ticks++;
      halt();

      if (need_reschedule() || get_runnable_tasks_count() > 0)
         schedule();
   }
}

void yield_until_last(void)
{
   ASSERT(is_preemption_enabled());

   /*
    * Wait until every other *non-worker* task has reached a stopping
    * point — SLEEPING via a wait primitive, or ZOMBIE via
    * kthread_exit — so a caller that inspects shared state next sees
    * the result of all in-flight work from ordinary tasks.
    *
    * Worker threads are a separate schedulable class for bottom-half
    * processing (see wth.c); they live in worker_threads[], not in
    * the runnable tree, and are therefore INVISIBLE to this
    * function. A worker chewing through queued jobs will NOT delay
    * our return. Callers that need worker quiescence too should
    * additionally call wth_wait_for_completion() on each worker they
    * care about.
    *
    * Idle is also invisible: it lives outside the runnable tree (see
    * the comment above runnable_tree_root). So count > 0 cleanly
    * means "at least one non-idle, non-worker task is RUNNABLE".
    */
   while (get_runnable_tasks_count() > 0)
      kernel_yield();
}

__attribute__((constructor))
static void create_kernel_process(void)
{
   static struct task_and_process tp;

   struct task *s_kernel_ti = &tp.main_task_obj;
   struct process *s_kernel_pi = &tp.process_obj;

   /* runnable_tree_root starts at NULL (empty tree) -- no init needed. */
   s_kernel_pi->pid = create_new_pid();
   s_kernel_ti->tid = create_new_kernel_tid();
   atomic_store(&s_kernel_pi->ref_count, 1);
   s_kernel_ti->pi = s_kernel_pi;
   init_task_lists(s_kernel_ti);
   init_process_lists(s_kernel_pi);

   s_kernel_ti->is_main_thread = true;
   s_kernel_ti->running_in_kernel = IN_SYSCALL_FLAG;
   memcpy(s_kernel_pi->str_cwd, "/", 2);

   atomic_store(&s_kernel_ti->state, TASK_STATE_SLEEPING);

   /*
    * kernel_process becomes curr at boot without going through
    * switch_to_task, so the usual sched_start_quantum() site
    * doesn't fire. Seed the slice here; nr_running is 0 at this
    * point so it lands at the SCHED_LATENCY value (no contenders).
    */
   sched_start_quantum(s_kernel_ti);

   kernel_process = s_kernel_ti;
   kernel_process_pi = s_kernel_ti->pi;

#ifndef UNIT_TEST_ENVIRONMENT
   if (!in_panic()) {
      bzero(s_kernel_ti->ti_arch, sizeof(s_kernel_ti->ti_arch));
      bzero(s_kernel_pi->pi_arch, sizeof(s_kernel_pi->pi_arch));
      add_task(kernel_process);
   }
#endif

   set_curr_task(kernel_process);
}

struct process *task_get_pi_opaque(struct task *ti)
{
   if (ti != NULL)
      return ti->pi;

   return NULL;
}

void process_set_tty(struct process *pi, void *t)
{
   pi->proc_tty = t;
}

void init_sched(void)
{
   int tid;
   ulong var;

   ASSERT(kernel_process_pi->pid == 0);
   ASSERT(kernel_process_pi->parent_pid == 0);

   kernel_process->pi->pdir = get_kernel_pdir();
   tid = kthread_create(&idle, 0, NULL);

   if (tid < 0)
      panic("Unable to create the idle_task!");

   idle_task = get_task(tid);

   /*
    * kthread_create() above ran with idle_task still NULL, so the
    * `ti == idle_task` guards in task_add_to_state_list() and
    * task_remove_from_state_list() didn't fire and idle was inserted
    * into the runnable tree. Pull it back out now that idle_task is
    * set — from this point forward those guards keep idle out for
    * good. Disable interrupts around the bintree_remove for the
    * same IRQ-vs-tree-mutation reason that add_task() does.
    */
   disable_interrupts(&var);
   {
      DEBUG_ONLY_UNSAFE(struct task *removed =)
         bintree_remove(&runnable_tree_root,
                        idle_task,
                        sched_runnable_cmp,
                        struct task,
                        runnable_tree_node);
      ASSERT(removed == idle_task);
      atomic_fetch_sub(&runnable_tasks_count, 1);

      /*
       * Mirror task_remove_from_state_list's sum adjustment so the
       * runnable_tasks_count and sum_vruntime_in_tree counters stay
       * coherent. idle's vruntime is 0 here so this is effectively a
       * no-op, but doing it keeps the invariant defensive against
       * future changes to idle's startup vruntime.
       */
      atomic_fetch_sub(&sum_vruntime_in_tree,
                       atomic_load(&idle_task->ticks.vruntime));
   }
   enable_interrupts(&var);
}

void set_current_task_in_kernel(void)
{
   ASSERT(!is_preemption_enabled());
   get_curr_task()->running_in_kernel |= IN_SYSCALL_FLAG;
}

static void task_add_to_state_list(struct task *ti)
{
   /*
    * Worker threads are a separate schedulable class for bottom-half
    * processing (see wth.c). They're tracked in worker_threads[] and
    * picked by wth_get_runnable_thread() — never via this tree — so
    * they intentionally don't show up in runnable_tasks_count
    * either.
    */
   if (is_worker_thread(ti))
      return;

   /*
    * idle_task lives outside the runnable tree: do_schedule() falls
    * back to it directly when the tree has no winner. Skipping idle
    * here keeps the tree's leftmost element a real candidate (no
    * skip-past-idle dance) and avoids the curr-RUNNING -> curr-
    * RUNNABLE transition trying to re-insert idle when idle is curr.
    *
    * Note on init order: kthread_create() for idle runs before
    * init_sched() assigns idle_task, so the very first add_task for
    * idle reaches this function with idle_task still NULL. That's
    * fine: idle goes into the tree once at boot, and init_sched()
    * pulls it out immediately after assigning idle_task. From then
    * on, this guard keeps idle out for good.
    */
   if (ti == idle_task)
      return;

   switch ((enum task_state) atomic_load(&ti->state)) {

      case TASK_STATE_RUNNABLE: {

         /*
          * Re-initialize the node's left/right before each insert.
          * bintree_insert() places `ti` at a slot but does NOT clear
          * ti's own bintree_node — it relies on the caller to hand
          * over a clean (leaf-ready) node. A prior insert/remove
          * cycle leaves stale left/right pointing at whatever
          * children ti had back then; reinserting with those stale
          * links yields a corrupted tree whose later removes (and
          * walks) chase dangling pointers into freed memory.
          *
          * (init_task_lists() does this once at task birth, which is
          * why the very first insert is clean. The other in-tree
          * trees in this file — tree_by_tid_root, tasks_waiting on
          * wobj — get a single insert per task lifetime, so they
          * don't need this guard. The runnable tree, by contrast,
          * sees a task come and go on every state transition.)
          */
         bintree_node_init(&ti->runnable_tree_node);

         DEBUG_ONLY_UNSAFE(bool inserted =)
            bintree_insert(&runnable_tree_root,
                           ti,
                           sched_runnable_cmp,
                           struct task,
                           runnable_tree_node);
         ASSERT(inserted);
         atomic_fetch_add(&runnable_tasks_count, 1);
         atomic_fetch_add(&sum_vruntime_in_tree,
                          atomic_load(&ti->ticks.vruntime));
         break;
      }

      case TASK_STATE_SLEEPING:
         /* no dedicated list */
         break;

      case TASK_STATE_RUNNING:
         /* no dedicated list: without SMP there's only one 'running' task */
         break;

      case TASK_STATE_ZOMBIE:
         /* no dedicated list */
         break;

      case TASK_STATE_STOPPED:
         /*
          * Stopped tasks live outside the runnable tree on
          * purpose — that's the whole point of having STOPPED be a
          * real state rather than a flag overlaid on RUNNABLE. They
          * become reachable again only via task_change_state(...,
          * RUNNABLE) when SIGCONT arrives (see action_continue() in
          * kernel/signal.c).
          */
         break;

      default:
         NOT_REACHED();
   }
}

static void task_remove_from_state_list(struct task *ti)
{
   /* Workers don't live in this tree — see task_add_to_state_list(). */
   if (is_worker_thread(ti))
      return;

   /* Neither does idle_task — see task_add_to_state_list(). */
   if (ti == idle_task)
      return;

   switch ((enum task_state) atomic_load(&ti->state)) {

      case TASK_STATE_RUNNABLE: {
         /*
          * prev is debug-only so it doesn't trip
          * -Werror=unused-but-set-variable in release builds, where
          * ASSERT() expands to do {} while (0) and never reads it.
          * DEBUG_ONLY_UNSAFE() expands to nothing in release, so the
          * partial-wrap on the assignment leaves the atomic_fetch_sub
          * unconditional while only capturing its return into `prev`
          * in debug.
          */
         DEBUG_ONLY(int prev);

         DEBUG_ONLY_UNSAFE(struct task *removed =)
            bintree_remove(&runnable_tree_root,
                           ti,
                           sched_runnable_cmp,
                           struct task,
                           runnable_tree_node);
         ASSERT(removed == ti);

         DEBUG_ONLY_UNSAFE(prev =)
            atomic_fetch_sub(&runnable_tasks_count, 1);
         ASSERT(prev >= 1);
         atomic_fetch_sub(&sum_vruntime_in_tree,
                          atomic_load(&ti->ticks.vruntime));
         break;
      }

      case TASK_STATE_SLEEPING:
         /* no dedicated list */
         break;

      case TASK_STATE_RUNNING:
         /* no dedicated list */
         break;

      case TASK_STATE_ZOMBIE:
         /* no dedicated list */
         break;

      case TASK_STATE_STOPPED:
         /*
          * STOPPED tasks live outside the runnable tree —
          * mirror image of the STOPPED case in
          * task_add_to_state_list(). Removing such a task is a
          * no-op, but we still hit this path when the state
          * machine transitions STOPPED -> RUNNABLE on SIGCONT or
          * on a killing signal (via signal_wakeup_task).
          */
         break;

      default:
         NOT_REACHED();
   }
}

/*
 * Variant of task_change_state() for callers that already run with
 * interrupts disabled. Skips the EFLAGS save/restore — useful inside an
 * existing disable_interrupts_forced() region (e.g. wth_run()'s sleep
 * prep) — but still enforces the same invariants the safe wrapper does.
 */
void task_change_state_unsafe(struct task *ti, enum task_state new_state)
{
   ASSERT(!are_interrupts_enabled());
   ASSERT(atomic_load(&ti->state) != (int) new_state);
   ASSERT(atomic_load(&ti->state) != TASK_STATE_ZOMBIE);

   task_remove_from_state_list(ti);
   atomic_store(&ti->state, (int) new_state);
   task_add_to_state_list(ti);
}

void task_change_state(struct task *ti, enum task_state new_state)
{
   ulong var;
   disable_interrupts(&var);
   {
      task_change_state_unsafe(ti, new_state);
   }
   enable_interrupts(&var);
}

void task_change_state_idempotent(struct task *ti, enum task_state new_state)
{
   ulong var;
   disable_interrupts(&var);
   {
      if (atomic_load(&ti->state) != (int) new_state)
         task_change_state_unsafe(ti, new_state);
   }
   enable_interrupts(&var);
}

void add_task(struct task *ti)
{
   ulong var;

   /*
    * IRQ-safe wrapper: task_add_to_state_list() touches the AVL
    * runnable_tree_root, and an IRQ-driven tick_all_timers() may
    * also mutate that tree via task_change_state(). Without
    * disable_interrupts(), an IRQ landing mid-rotation here would
    * see a half-updated tree and could insert against stale links.
    * disable_preemption() alone (the old guard from the list-based
    * scheduler) is not strong enough now that the runnable container
    * is a tree.
    */
   disable_interrupts(&var);
   {
      task_add_to_state_list(ti);

      bintree_insert_ptr(&tree_by_tid_root,
                         ti,
                         struct task,
                         tree_by_tid_node,
                         tid);
   }
   enable_interrupts(&var);
}

void remove_task(struct task *ti)
{
   disable_preemption();
   {
      ASSERT_TASK_STATE(atomic_load(&ti->state), TASK_STATE_ZOMBIE);

      task_remove_from_state_list(ti);

      bintree_remove_ptr(&tree_by_tid_root,
                         ti,
                         struct task,
                         tree_by_tid_node,
                         tid);

      free_task(ti);
   }
   enable_preemption();
}

/*
 * Dynamic per-quantum slice (subticks). Equal-slice instantiation
 * of EEVDF: every task gets SCHED_LATENCY / N clamped at MIN_GRAN.
 * General EEVDF allows per-task slice_i (interactive tasks request
 * short slices; throughput tasks request long ones). See
 * docs/scheduler.md.
 *
 * nr_running = runnable_tasks_count + 1: the runnable container
 * excludes curr (and idle and workers), so we fold curr back in.
 */
static u32 sched_compute_slice(void)
{
   const u32 nr_running = (u32)get_runnable_tasks_count() + 1;
   return MAX(SCHED_LATENCY_TICKS * VRUNTIME_SCALE / nr_running,
              (u32)MIN_GRANULARITY_TICKS * VRUNTIME_SCALE);
}

/*
 * EEVDF "virtual time" V: weighted mean vruntime over all runnable
 * tasks + curr. A task is eligible when v_i <= V (it has not
 * consumed more than its fair share).
 *
 * Equal-weight collapse: simple mean over (sum_in_tree + curr_v) /
 * nr_running. General form V = sum(w_i * v_i) / sum(w_i). The
 * computation is on demand; per-tick vruntime growth on curr is
 * reflected via the curr_v read here (no separate maintenance).
 * See docs/scheduler.md.
 */
STATIC u64 sched_compute_avg_vruntime(void)
{
   const u32 nr_running = (u32)get_runnable_tasks_count() + 1;
   struct task *curr = get_curr_task();
   const u64 curr_v = atomic_load(&curr->ticks.vruntime);
   const u64 sum = atomic_load(&sum_vruntime_in_tree);

   return (sum + curr_v) / nr_running;
}

/*
 * EEVDF eligibility predicate: a task is eligible at virtual time
 * V if its vruntime has not yet caught up to V (it has consumed at
 * or below its fair share). Ineligible tasks are skipped by the
 * selector until V advances enough to bring them back.
 *
 * Predicate is unchanged under per-task weights -- only V's
 * definition (sched_compute_avg_vruntime) generalizes. See
 * docs/scheduler.md.
 */
STATIC bool sched_is_eligible(struct task *ti)
{
   const u64 v = atomic_load(&ti->ticks.vruntime);
   return v <= sched_compute_avg_vruntime();
}

/*
 * Refresh `ti`'s EEVDF slice request and virtual deadline. Called
 * at every RUNNABLE-entry (fork, wake, preemption) and at quantum
 * start, so the deadline field stays consistent with the task's
 * current vruntime.
 *
 * Equal-weight collapse: deadline = vruntime + slice. With per-task
 * weights it'd be vruntime + slice / w.
 */
static void sched_refresh_slice_deadline(struct task *ti)
{
   ti->ticks.slice = sched_compute_slice();
   atomic_store(&ti->ticks.deadline,
                atomic_load(&ti->ticks.vruntime) + ti->ticks.slice);
}

/*
 * Roadmap step 4: fork vruntime handoff. Initialize a freshly-
 * allocated task's vruntime to the current `min_vruntime`. A new
 * fork (or new kernel thread) would otherwise start at 0 and
 * leapfrog every accumulated runnable task until it caught up to
 * the leading edge. Called from the new-task paths in process.c
 * (allocate_new_process / allocate_new_thread).
 *
 * No BONUS, unlike wake_vruntime_handoff(): a fresh task shouldn't
 * be more privileged than already-runnable tasks. It starts on par
 * with the leading edge.
 */
void fork_vruntime_handoff(struct task *ti)
{
   atomic_store(&ti->ticks.vruntime, atomic_load(&min_vruntime));
   sched_refresh_slice_deadline(ti);
}

/*
 * Begin a new EEVDF quantum on `ti`: reset the per-tick counter
 * and freeze the quantum's slice budget + deadline. Called from
 * switch_to_task() on a real task switch and from do_schedule()'s
 * keep-curr branch. The slice is frozen for the duration of the
 * quantum -- subsequent wake/sleep activity by other tasks may
 * change nr_running but doesn't re-fair this quantum.
 */
void sched_start_quantum(struct task *ti)
{
   ti->ticks.slice_used = 0;
   sched_refresh_slice_deadline(ti);
}

/*
 * Roadmap step 2: wakeup vruntime handoff. Raise `ti`'s vruntime to
 * `max(vruntime, min_vruntime - WAKEUP_VRUNTIME_BONUS)` with underflow
 * guard at 0. Called from wake_up() (wobj.c) and tick_all_timers()
 * (timer.c) on the SLEEPING -> RUNNABLE transition. Monotonic raise:
 * never decreases the task's vruntime.
 *
 * No-op if the task's vruntime is already above the floor (woken
 * before min_vruntime had a chance to outrun it).
 */
void wake_vruntime_handoff(struct task *ti)
{
   ulong var;

   /*
    * Tree invariant: a task's vruntime is the tree key when it's in
    * the runnable tree, so we must NOT mutate it while ti is in the
    * tree (state == RUNNABLE). Restrict the raise to "ti is still
    * SLEEPING", under IRQ-disabled so the state can't flip between
    * the check and the store. A double-wake (wake_up() called on an
    * already-RUNNABLE task because a peer wake won the wait_obj
    * reset first) reaches this function with state == RUNNABLE and
    * is correctly turned into a no-op.
    */
   disable_interrupts(&var);
   {
      if (atomic_load(&ti->state) != TASK_STATE_SLEEPING)
         goto out;

      const u64 current_min = atomic_load(&min_vruntime);
      const u64 floor = current_min > WAKEUP_VRUNTIME_BONUS
                           ? current_min - WAKEUP_VRUNTIME_BONUS
                           : 0;

      if (atomic_load(&ti->ticks.vruntime) < floor)
         atomic_store(&ti->ticks.vruntime, floor);

      /*
       * Recompute slice + deadline now that vruntime is finalized.
       * The task is still SLEEPING (won't enter the runnable tree
       * until task_change_state_idempotent below), so this happens
       * before any selector can read the deadline.
       */
      sched_refresh_slice_deadline(ti);
   }
out:
   enable_interrupts(&var);
}

void sched_account_ticks(void)
{
   struct task *curr = get_curr_task();
   const enum task_state state = get_curr_task_state();
   const bool is_running = (state == TASK_STATE_RUNNING);
   const bool is_worker = is_worker_thread(curr);
   struct sched_ticks *t = &curr->ticks;

   ASSERT(curr != NULL);
   ASSERT(!is_preemption_enabled());

   t->slice_used += VRUNTIME_SCALE;
   t->total++;

   if (curr->running_in_kernel)
      t->total_kernel++;

   if (is_running && curr != idle_task) {

      /*
       * vruntime is "CPU time consumed by this task", incremented
       * each tick the task is RUNNING (idle excluded -- idle's CPU
       * time is "free"). Stored in subticks (VRUNTIME_SCALE per
       * real tick) so the slice math in the timeout check below can
       * keep useful resolution when SCHED_LATENCY_TICKS / nr_running
       * would otherwise truncate hard.
       *
       * Earlier in the roadmap, the increment was
       * `runnable_tasks_count - 1` (i.e. weighted by the number of
       * other non-idle waiters). That was an implicit fairness
       * compensation -- pre-step-4, a freshly forked task started
       * at vruntime 0 and would have leapfrogged everyone, so we
       * leaned on the running task's vruntime growing faster under
       * contention to keep the gap reasonable. Steps 2 and 4 (wake
       * + fork handoffs to min_vruntime) now do that compensation
       * explicitly: a woken or forked task lands at the leading
       * edge, not at zero. Leaving the weighted formula in place
       * on top would double-count.
       *
       * Picking the task with the lowest vruntime is fairer than
       * picking the lowest `total` ticks, because tasks that
       * monopolized the CPU while nothing else wanted it aren't
       * penalized for it later. (That property holds for both
       * weighted and unweighted vruntime, since SLEEPING tasks
       * accumulate nothing either way.)
       *
       * The is_running gate is load-bearing because vruntime is the
       * runnable-tree key while curr is RUNNABLE. Between
       * task_change_state(curr, RUNNABLE) inside do_schedule() and
       * set_curr_task(selected) inside switch_to_task(),
       * get_curr_task() still returns the already-RUNNABLE outgoing
       * task -- and that task now sits in the tree. A timer IRQ in
       * that window would otherwise mutate the in-tree task's key
       * out from under bintree_remove() and the next remove for it
       * would chase a stale path. With this gate, the increment
       * only fires while curr is genuinely RUNNING (outside the
       * tree).
       */
      atomic_fetch_add(&t->vruntime, VRUNTIME_SCALE);

      /*
       * Roadmap step 1: monotonic high-watermark min_vruntime.
       * Step 2 (wakeup handoff) uses this as the floor for the
       * woken task's vruntime. The guard makes the store
       * structurally monotonic -- no decrease can ever happen.
       * Like vruntime itself, min_vruntime is in subticks.
       */
      const u64 vruntime = atomic_load(&t->vruntime);

      if (vruntime > atomic_load(&min_vruntime))
         atomic_store(&min_vruntime, vruntime);
   }

   /*
    * EEVDF slice budget for the current quantum. Frozen at quantum
    * start by sched_start_quantum() so a wake/sleep by another task
    * mid-quantum doesn't re-fair curr's quantum length. Workers
    * have unlimited slice -- they're preempted only by another
    * worker, never by the timer-based timeout.
    */
   const bool timeout = !is_worker && t->slice_used >= t->slice;

   /*
    * !is_running covers all the cases where curr can't keep running
    * (SLEEPING, ZOMBIE, STOPPED) — including SIGSTOP, which transitions
    * the task to TASK_STATE_STOPPED. No separate `stopped` check needed.
    */
   if (!is_running || timeout)
      sched_set_need_resched();
}

static bool
sched_should_return_immediately(struct task *curr, enum task_state curr_state)
{
   if (UNLIKELY(in_panic()))
      return true;

   if (UNLIKELY(curr_state == TASK_STATE_ZOMBIE))
      return false;

   if (UNLIKELY(curr->timer_ready)) {

      /*
       * Corner case: call to the scheduler with timer_ready set.
       *
       * This means that we called task_set_wakeup_timer(), got preempted,
       * the timer fired and only THEN we went to sleep. This is a perfectly
       * valid case. We might also have already set the state to SLEEPING.
       *
       * Because the timer handler couldn't wake this task up, as it was already
       * running, the right behavior is just to not going to sleep the first
       * time we're supposed to. That's AS IF we went to sleep as expected and
       * woke up very quickly after that. Just don't go to sleep at all.
       */

      if (curr_state != TASK_STATE_RUNNING) {
         task_change_state(curr, TASK_STATE_RUNNING);
      }

      curr->timer_ready = false;
      return true; /* Give the control back to current task */
   }

   return false;
}

STATIC struct task *
sched_do_select_runnable_task(enum task_state curr_state, bool resched)
{
   struct task *selected;
   ulong var;
   struct task *curr = get_curr_task();

   /*
    * EEVDF selection: pick the eligible task with the earliest
    * virtual deadline. Equal-slice + equal-weight collapse in
    * Tilck today:
    *
    *   - deadline = vruntime + slice, with slice uniform across all
    *     tasks (SCHED_LATENCY / N clamped at MIN_GRAN), so ordering
    *     by deadline is identical to ordering by vruntime.
    *   - leftmost (vruntime, tid) is therefore the min-deadline
    *     task in the tree.
    *   - min vruntime <= mean vruntime by definition over its own
    *     set, so under equal weights the leftmost is normally
    *     eligible too -- a single bintree_get_first_obj() lookup
    *     gives the EEVDF answer in O(log N).
    *
    * When slice diverges per task, deadline order decouples from
    * vruntime order; the leftmost-by-vruntime may not be the
    * min-deadline task and may need an in-order walk skipping
    * ineligibles, or a tree augmented with subtree-min-eligible-
    * deadline. Both are future work tied to per-task slice support.
    * See docs/scheduler.md.
    *
    * On the eligibility predicate: `v_i <= avg_vruntime` is a
    * coarse approximation. Under the wake-handoff transient (a
    * just-woken curr drags avg below a long-RUNNABLE peer), a tree
    * task can be temporarily flagged ineligible even though the
    * algorithm should still pick it. EEVDF's own "if none eligible,
    * pick leftmost" fallback covers this: same task picked either
    * way, no behavior difference. A more precise predicate would
    * need per-task lag computed against an avg-with-load-tracking
    * baseline -- worth picking up only if/when per-task weights or
    * slices land. See docs/scheduler.md.
    *
    * Disable interrupts around the descent: do_schedule() may run
    * with IRQs on (irq_resched() re-enables them before calling us),
    * and tree mutations from IRQ context (tick_all_timers waking a
    * sleeper -> task_change_state -> bintree_insert) include AVL
    * rotations that briefly leave links inconsistent. Reading
    * LEFT_OF mid-rotation would dereference a stale pointer.
    */
   disable_interrupts(&var);
   {
      selected = bintree_get_first_obj(runnable_tree_root,
                                       struct task,
                                       runnable_tree_node);
   }
   enable_interrupts(&var);

   if (selected) {
      ASSERT_TASK_STATE(atomic_load(&selected->state), TASK_STATE_RUNNABLE);
   }

   /*
    * Tree was empty. Keep running curr if it still wants to;
    * otherwise let do_schedule() fall back to idle.
    */
   if (!selected) {

      if (curr_state == TASK_STATE_RUNNING)
         selected = curr;
   }

   if (!resched && selected && curr != idle_task) {

      /*
       * If need_resched is not set, the caller didn't want necessarily to
       * yield, unless the current task is the idle task. In that case, always
       * yield to any other task.
       */

      if (curr_state == TASK_STATE_RUNNING) {

         const u64 curr_vruntime = atomic_load(&curr->ticks.vruntime);
         const u64 selected_vruntime = atomic_load(&selected->ticks.vruntime);

         if (curr_vruntime < selected_vruntime)
            selected = curr;
      }
   }

   return selected;
}

void do_schedule(void)
{
   struct task *selected = NULL;
   enum task_state curr_state = get_curr_task_state();
   const bool resched = need_reschedule();
   struct task *curr = get_curr_task();

   ASSERT(!is_preemption_enabled());

   /* Essential: clear the `__need_resched` flag */
   sched_clear_need_resched();

   /* Handle special corner cases */
   if (sched_should_return_immediately(curr, curr_state))
      return;

   /*
    * Workers are picked here, BEFORE the regular runnable-list lookup
    * below. They're a separate schedulable class for bottom-half
    * processing (see wth.c), and a runnable worker always wins
    * against a runnable non-worker.
    */
   selected = wth_get_runnable_thread();

   /* Check for regular runnable tasks */
   if (!selected) {

      selected = sched_do_select_runnable_task(curr_state, resched);

      if (!selected)
         selected = idle_task; /* fall-back to the idle task */
   }

   if (selected != curr) {

      /* If we preempted the process, it is still `running` */
      if (curr_state == TASK_STATE_RUNNING) {

         /*
          * Refresh deadline before re-entering the runnable tree:
          * the quantum is ending, vruntime has grown, and the new
          * deadline (vruntime + next-quantum slice) is what an
          * EEVDF selector will compare against.
          */
         sched_refresh_slice_deadline(curr);
         task_change_state(curr, TASK_STATE_RUNNABLE);
      }

      /* A task switch is required */
      switch_to_task(selected);

   } else {

      /*
       * Two paths reach here. Common: the normal "keep running curr"
       * outcome -- curr had the lowest vruntime and we didn't pick
       * anyone else. Rare: a timer IRQ ran tick_all_timers() while
       * we were iterating the runnable list and woke curr (state
       * SLEEPING -> RUNNABLE, vruntime raised by the wakeup handoff,
       * timer_ready set); the vruntime-min iteration then picked
       * curr back. Normalize state + clear timer_ready so a later
       * sleep doesn't short-circuit via sched_should_return_immediately().
       */
      task_change_state_idempotent(curr, TASK_STATE_RUNNING);
      curr->timer_ready = false;

      if (LIKELY(!pending_signals())) {

         /* Keep-curr: start a new quantum (resets slice_used + slice). */
         sched_start_quantum(selected);

      } else {

         /* There are pending signals: do a complete task switch */
         switch_to_task(selected);
      }
   }

   ASSERT(curr_state != TASK_STATE_ZOMBIE);
}

struct task *get_task(int tid)
{
   long ltid = tid;
   struct task *res = NULL;

   ASSERT(!is_preemption_enabled());

   res = bintree_find_ptr(tree_by_tid_root,
                          ltid,
                          struct task,
                          tree_by_tid_node,
                          tid);

   return res;
}

struct process *get_process(int pid)
{
   struct task *ti;
   ASSERT(!is_preemption_enabled());

   ti = get_task(pid);

   if (ti && !is_kernel_thread(ti))
      return ti->pi;

   return NULL;
}

bool in_currently_dying_task(void)
{
   return get_curr_task_state() == TASK_STATE_ZOMBIE;
}

int send_signal_to_group(int pgid, int sig)
{
   struct bintree_walk_ctx ctx;
   struct task *ti;
   struct process *leader = NULL;
   int count = 0;
   struct process *curr_pi = get_curr_proc();

   disable_preemption();

   bintree_in_order_visit_start(&ctx,
                                tree_by_tid_root,
                                struct task,
                                tree_by_tid_node,
                                false);

   while ((ti = bintree_in_order_visit_next(&ctx))) {

      struct process *pi = ti->pi;

      if (pi->pgid == pgid && pi != curr_pi && pi->pid != 1) {

         if (pi->pid != pgid)
            send_signal(pi->pid, sig, true);
         else
            leader = pi;

         count++;
      }
   }

   if (leader)
      send_signal(leader->pid, sig, true); /* kill the leader last */

   enable_preemption();

   if (curr_pi->pgid == pgid) {

      /* kill the current process, as _very_ last */
      send_signal(curr_pi->pid, sig, true);
      count++;
   }

   return count > 0 ? 0 : -ESRCH;
}

int send_signal_to_session(int sid, int sig)
{
   struct bintree_walk_ctx ctx;
   struct task *ti;
   struct process *leader = NULL;
   int count = 0;
   struct process *curr_pi = get_curr_proc();

   disable_preemption();

   bintree_in_order_visit_start(&ctx,
                                tree_by_tid_root,
                                struct task,
                                tree_by_tid_node,
                                false);

   while ((ti = bintree_in_order_visit_next(&ctx))) {

      struct process *pi = ti->pi;

      if (pi->pgid == sid && pi != curr_pi && pi->pid != 1) {

         if (pi->pid != sid)
            send_signal(pi->pid, sig, true);
         else
            leader = pi;

         count++;
      }
   }

   if (leader)
      send_signal(leader->pid, sig, true); /* kill the leader last */

   enable_preemption();

   /* kill the current process, as _very_ last */
   if (curr_pi->pgid == sid) {
      send_signal(curr_pi->pid, sig, true);
      count++;
   }

   return count > 0 ? 0 : -ESRCH;
}
