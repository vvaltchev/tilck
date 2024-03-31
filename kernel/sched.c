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

/* Shared global variables */
struct task *__current;
ATOMIC(int) __disable_preempt = 1;        /* see docs/atomics.md */
ATOMIC(int) __need_resched;               /* see docs/atomics.md */

struct task *kernel_process;
struct process *kernel_process_pi;

/* Task lists */
struct list runnable_tasks_list;

/* Static variables */
static struct task *tree_by_tid_root;
static u64 idle_ticks;
static volatile int runnable_tasks_count;
static int current_max_pid = -1;
static int current_max_kernel_tid = -1;
struct task *idle_task;

const char *const task_state_str[5] = {
   [TASK_STATE_INVALID]  = "invalid",
   [TASK_STATE_RUNNABLE] = "runnable",
   [TASK_STATE_RUNNING]  = "running",
   [TASK_STATE_SLEEPING] = "sleeping",
   [TASK_STATE_ZOMBIE]   = "zombie",
};

void enable_preemption(void)
{
   int oldval =
      atomic_fetch_sub_explicit(&__disable_preempt, 1, mo_relaxed);

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
   ASSERT(!is_preemption_enabled());

   struct create_pid_visit_ctx ctx = {
      .kernel_tid = false,
      .id_off = 0,
      .lowest_available = 0,
      .lowest_after_current_max = current_max_pid + 1,
   };

   int r = create_id_common(&ctx, MAX_PID);

   if (r >= 0)
      current_max_pid = r;

   return r;
}

int create_new_kernel_tid(void)
{
   ASSERT(!is_preemption_enabled());

   struct create_pid_visit_ctx ctx = {
      .kernel_tid = true,
      .id_off = KERNEL_TID_START,
      .lowest_available = 0,
      .lowest_after_current_max = current_max_kernel_tid + 1,
   };

   int r = create_id_common(&ctx, KERNEL_MAX_TID);

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

static void idle(void)
{
   while (true) {

      ASSERT(is_preemption_enabled());

      idle_ticks++;
      halt();

      if (need_reschedule() || runnable_tasks_count > 1)
         schedule();
   }
}

void yield_until_last(void)
{
   ASSERT(is_preemption_enabled());

   do { kernel_yield(); } while (runnable_tasks_count > 2);
   kernel_yield();
}

__attribute__((constructor))
static void create_kernel_process(void)
{
   static char kernel_proc_buf[
      sizeof(struct process) + sizeof(struct task)
   ] ALIGNED_AT(sizeof(void *));

   struct task *s_kernel_ti = (struct task *)kernel_proc_buf;
   struct process *s_kernel_pi = (struct process *)(s_kernel_ti + 1);

   list_init(&runnable_tasks_list);
   s_kernel_pi->pid = create_new_pid();
   s_kernel_ti->tid = create_new_kernel_tid();
   s_kernel_pi->ref_count = 1;
   s_kernel_ti->pi = s_kernel_pi;
   init_task_lists(s_kernel_ti);
   init_process_lists(s_kernel_pi);

   s_kernel_ti->is_main_thread = true;
   s_kernel_ti->running_in_kernel = true;
   memcpy(s_kernel_pi->str_cwd, "/", 2);

   s_kernel_ti->state = TASK_STATE_SLEEPING;

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

   ASSERT(kernel_process_pi->pid == 0);
   ASSERT(kernel_process_pi->parent_pid == 0);

   kernel_process->pi->pdir = get_kernel_pdir();
   tid = kthread_create(&idle, 0, NULL);

   if (tid < 0)
      panic("Unable to create the idle_task!");

   idle_task = get_task(tid);
}

void set_current_task_in_kernel(void)
{
   ASSERT(!is_preemption_enabled());
   get_curr_task()->running_in_kernel = true;
}

static void task_add_to_state_list(struct task *ti)
{
   if (is_worker_thread(ti))
      return;

   switch (atomic_load_explicit(&ti->state, mo_relaxed)) {

      case TASK_STATE_RUNNABLE:
         list_add_tail(&runnable_tasks_list, &ti->runnable_node);
         runnable_tasks_count++;
         break;

      case TASK_STATE_SLEEPING:
         /* no dedicated list */
         break;

      case TASK_STATE_RUNNING:
         /* no dedicated list: without SMP there's only one 'running' task */
         break;

      case TASK_STATE_ZOMBIE:
         /* no dedicated list */
         break;

      default:
         NOT_REACHED();
   }
}

static void task_remove_from_state_list(struct task *ti)
{
   if (is_worker_thread(ti))
      return;

   switch (atomic_load_explicit(&ti->state, mo_relaxed)) {

      case TASK_STATE_RUNNABLE:
         list_remove(&ti->runnable_node);
         runnable_tasks_count--;
         ASSERT(runnable_tasks_count >= 0);
         break;

      case TASK_STATE_SLEEPING:
         /* no dedicated list */
         break;

      case TASK_STATE_RUNNING:
         /* no dedicated list */
         break;

      case TASK_STATE_ZOMBIE:
         /* no dedicated list */
         break;

      default:
         NOT_REACHED();
   }
}

void task_change_state(struct task *ti, enum task_state new_state)
{
   ulong var;
   ASSERT(ti->state != new_state);
   ASSERT(ti->state != TASK_STATE_ZOMBIE);

   disable_interrupts(&var);
   {
      task_remove_from_state_list(ti);
      atomic_store_explicit(&ti->state, new_state, mo_relaxed);
      task_add_to_state_list(ti);
   }
   enable_interrupts(&var);
}

void task_change_state_idempotent(struct task *ti, enum task_state new_state)
{
   ulong var;
   disable_interrupts(&var);
   {
      if (atomic_load_explicit(&ti->state, mo_relaxed) != new_state) {
         task_change_state(ti, new_state);
      }
   }
   enable_interrupts(&var);
}

void add_task(struct task *ti)
{
   disable_preemption();
   {
      task_add_to_state_list(ti);

      bintree_insert_ptr(&tree_by_tid_root,
                         ti,
                         struct task,
                         tree_by_tid_node,
                         tid);
   }
   enable_preemption();
}

void remove_task(struct task *ti)
{
   disable_preemption();
   {
      ASSERT_TASK_STATE(ti->state, TASK_STATE_ZOMBIE);

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

void sched_account_ticks(void)
{
   struct task *curr = get_curr_task();
   const enum task_state state = get_curr_task_state();
   const bool is_running = (state == TASK_STATE_RUNNING);
   const bool is_worker = is_worker_thread(curr);
   struct sched_ticks *t = &curr->ticks;

   ASSERT(curr != NULL);
   ASSERT(!is_preemption_enabled());

   t->timeslice++;
   t->total++;

   if (curr->running_in_kernel)
      t->total_kernel++;

   if (curr != idle_task) {

      /*
       * The more currently runnable tasks are, the higher vruntime has to
       * grow: if case of just 1 runnable task (+1 for idle ignored), vruntime
       * will increase by just +1. In case of 15 runnable tasks, vruntime will
       * increase by +15. The logic behind is the following: supposing all the
       * 15 tasks are runnable and they all start with vruntime = 0, after the
       * first has run, it will have vruntime = 15 * TIME_SLICE_TICKS and will
       * have to wait until all the other 14 tasks ran until it can be picked
       * again.
       *
       * Now, picking the task with the lowest vruntime will be more fair than
       * picking the task with the lowest `total` number of ticks, because
       * tasks that that consumed 100% of the CPU when no other task was
       * runnable won't be so much penalized.
       */
      t->vruntime += (u64)(runnable_tasks_count - 1);
   }

   /*
    * need_resched is never set for worker threads when they used too much
    * CPU time: their timeslice is unlimited and can preempted only be another
    * worker thread.
    */
   const bool timeout = !is_worker && t->timeslice >= TIME_SLICE_TICKS;

   if (curr->stopped || !is_running || timeout)
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

static struct task *
sched_do_select_runnable_task(enum task_state curr_state, bool resched)
{
   struct task *curr = get_curr_task();
   struct task *selected = NULL;
   struct task *pos;

   list_for_each_ro(pos, &runnable_tasks_list, runnable_node) {

      ASSERT_TASK_STATE(pos->state, TASK_STATE_RUNNABLE);

      if (pos->stopped || pos == idle_task)
         continue;

      if (pos->timer_ready) {
         selected = pos;
         break;
      }

      if (!selected || pos->ticks.vruntime < selected->ticks.vruntime)
         selected = pos;
   }

   /* If there is still no selected task, check for current task */
   if (!selected) {

      if (curr_state == TASK_STATE_RUNNING && !curr->stopped)
         selected = curr;
   }

   if (!resched && selected && curr != idle_task) {

      /*
       * If need_resched is not set, the caller didn't want necessarily to
       * yield, unless the current task is the idle task. In that case, always
       * yield to any other task.
       */

      if (curr_state == TASK_STATE_RUNNING && !curr->stopped)
         if (curr->ticks.vruntime < selected->ticks.vruntime)
            selected = curr;
   }

   return selected;
}

void do_schedule(void)
{
   enum task_state curr_state = get_curr_task_state();
   const bool resched = need_reschedule();
   struct task *curr = get_curr_task();
   struct task *selected = NULL;

   ASSERT(!is_preemption_enabled());

   /* Essential: clear the `__need_resched` flag */
   sched_clear_need_resched();

   /* Handle special corner cases */
   if (sched_should_return_immediately(curr, curr_state))
      return;

   /* Check for worker threads ready to run */
   selected = wth_get_runnable_thread();

   /* Check for regular runnable tasks */
   if (!selected) {

      selected = sched_do_select_runnable_task(curr_state, resched);

      if (!selected)
         selected = idle_task; /* fall-back to the idle task */
   }

   if (selected != curr) {

      /* Sanity check */
      ASSERT(!selected->stopped);

      /* If we preempted the process, it is still `running` */
      if (curr_state == TASK_STATE_RUNNING)
         task_change_state(curr, TASK_STATE_RUNNABLE);

      /* A task switch is required */
      switch_to_task(selected);

   } else {

      if (LIKELY(!pending_signals())) {

         /* Just reset the current timeslice */
         selected->ticks.timeslice = 0;

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
   struct process *curr_pi = get_curr_proc();
   struct process *leader = NULL;
   struct bintree_walk_ctx ctx;
   struct task *ti;
   int count = 0;

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
   struct process *curr_pi = get_curr_proc();
   struct process *leader = NULL;
   struct bintree_walk_ctx ctx;
   struct task *ti;
   int count = 0;

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
