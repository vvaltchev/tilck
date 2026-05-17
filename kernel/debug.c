/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/self_tests.h>

#if KRN_HANG_DETECTION
   #include <tilck/kernel/process.h>
   #include <tilck/kernel/sync.h>
   #include <tilck/kernel/list.h>
   #include <tilck/kernel/pipe.h>
#endif

#include <elf.h>         // system header
#include <multiboot.h>   // system header in include/system_headers

/*
 * The sched-alive thread (kopt_sched_alive_thread, also known as the -sat
 * cmdline option) is a tiny kernel thread that wakes up periodically and
 * prints a heartbeat line. Its real value is not the heartbeat itself but
 * giving a regular execution context where, if compiled in, the hang
 * detector can sample the scheduler's forward progress and dump every
 * task's state when nothing is moving.
 *
 * Toggling sched_alive_thread_enabled at runtime (via the dedicated
 * syscall) silences the heartbeat without tearing the thread down — used
 * by tests that want a deterministic console.
 */
static volatile bool sched_alive_thread_enabled = true;

#if KRN_HANG_DETECTION

/* ============================================================
 *
 *                  HANG DETECTION FACILITY
 *
 * Forward-progress watchdog for stress-test debugging. Composed of:
 *
 *   (a) g_total_task_switches in kernel/switch.c  -- a single u64
 *       counter incremented on every real switch_to_task() that
 *       actually changes the running task. Cheap, no lock.
 *
 *   (b) sched_alive_thread() below -- a kernel thread that wakes up
 *       every SAT_INTERVAL_TICKS ticks, samples (a), and treats a
 *       near-zero delta as "the system is wedged".
 *
 *   (c) debug_dump_all_tasks_state() -- on a wedge, walk every task
 *       and print state, wait_obj, kind-specific decode, open pipe
 *       fds, and per-pipe history. Aimed at lost-wakeup,
 *       missed-broadcast and deadlock bugs.
 *
 * The whole thing is opt-in via KRN_HANG_DETECTION at build time AND
 * -sat at boot time. With KRN_HANG_DETECTION=OFF (the default) the
 * code below is compiled out and the alive thread degrades to its
 * legacy "I'm alive" heartbeat.
 *
 * ============================================================ */

/* One-word names for the kernel's wait-object kinds. Keep in sync with
 * enum wo_type. Indexed by ti->wobj.type. */
static const char *const wobj_type_str[] = {
   [WOBJ_NONE]       = "none",
   [WOBJ_KMUTEX]     = "kmutex",
   [WOBJ_KCOND]      = "kcond",
   [WOBJ_TASK]       = "task",
   [WOBJ_SEM]        = "sem",
   [WOBJ_MWO_WAITER] = "mwo_w",
   [WOBJ_MWO_ELEM]   = "mwo_e",
};

/*
 * Threshold below which we declare the system "stuck": a healthy kernel
 * does thousands of context switches per second, even when only running
 * the alive thread + idle (well over 100). When we drop below this for
 * an alive-thread interval, something is wedged and dumping the full
 * task state usually points straight at the wait_obj nobody woke up.
 *
 * The threshold is per-interval, so when SAT_INTERVAL_TICKS shrinks
 * the threshold has to shrink with it. Tuned for the default 250 ms
 * interval — adjust together if you change one.
 */
#define SAT_INTERVAL_TICKS         (KRN_TIMER_HZ / 4) /* 250 ms guest time */
#define SAT_STUCK_SWITCH_THRESHOLD 25                 /* ~100/sec scaled  */

/*
 * Number of warm-up intervals to skip before letting the watchdog fire.
 * Boot-time activity can be unusually quiet (everyone's asleep waiting
 * for the first real work), so the first few samples can falsely look
 * like a wedge.
 */
#define SAT_WARMUP_INTERVALS       2

/*
 * Iterator callback: called once per task by debug_dump_all_tasks_state.
 * Prints a single multi-segment line summarizing the task's state plus,
 * for waiting tasks, what kind of object it's parked on and a
 * one-glance summary of that object's relevant inner state.
 *
 * For non-kernel tasks, also enumerates open pipe fds (because the
 * "stuck reader / vanished writer" pattern needs both sides to be
 * visible). When the wait_obj is a kcond/kmutex of a known live pipe,
 * delegates to debug_dump_pipe_state_for_obj() to print that pipe's
 * counts + recent dup/close history.
 *
 * Runs under disable_preemption(); MUST NOT block. printk is OK because
 * it's preempt-safe.
 */
static int dump_task_state_cb(void *obj, void *arg)
{
   struct task *ti = obj;
   const char *name = "?";

   if (is_kernel_thread(ti)) {
      name = ti->kthread_name ? ti->kthread_name : "kthread";
   } else if (ti->pi && ti->pi->debug_cmdline) {
      name = ti->pi->debug_cmdline;
   }

   printk(NO_PREFIX "  tid=%d pid=%d name=%s state=%s",
          ti->tid,
          ti->pi ? ti->pi->pid : 0,
          name,
          task_state_str[ti->state]);

   if (ti->wobj.type != WOBJ_NONE) {

      void *p = wait_obj_get_ptr(&ti->wobj);
      const char *wt =
         ti->wobj.type < ARRAY_SIZE(wobj_type_str)
            ? wobj_type_str[ti->wobj.type]
            : "?";

      printk(NO_PREFIX " wobj=%s(%p)", wt, p);

      /*
       * Per-kind decode. Each branch prints what's most likely to
       * pinpoint a wedge: who holds the lock (kmutex), what the
       * counter is (sem), how many waiters are queued (kcond), what
       * tid we're waiting on (task). Add new branches here when a new
       * sync primitive's lost-wakeup pattern shows up.
       */
      if (ti->wobj.type == WOBJ_KMUTEX && p) {
         struct kmutex *m = p;
         struct task *o = m->owner_task;
         if (o)
            printk(NO_PREFIX " owner_tid=%d", o->tid);
      } else if (ti->wobj.type == WOBJ_SEM && p) {
         struct ksem *s = p;
         printk(NO_PREFIX " sem_counter=%d need_units=%u",
                s->counter, ti->wobj.extra);
      } else if (ti->wobj.type == WOBJ_KCOND && p) {
         struct kcond *c = p;
         struct wait_obj *wo;
         int n_waiters = 0;
         list_for_each_ro(wo, &c->wait_list, wait_list_node)
            n_waiters++;
         printk(NO_PREFIX " cond_waiters=%d", n_waiters);
      } else if (ti->wobj.type == WOBJ_TASK) {
         printk(NO_PREFIX " waiting_tid=%ld", wait_obj_get_data(&ti->wobj));
      }
   }

   if (ti->ticks_before_wake_up > 0)
      printk(NO_PREFIX " timer=%u", ti->ticks_before_wake_up);

   if (ti->timer_ready)
      printk(NO_PREFIX " timer_ready");

   if (ti->stop_pending)
      printk(NO_PREFIX " stop_pending");

   printk(NO_PREFIX "\n");

   /*
    * For user tasks, list any open fds that are pipe ends — including
    * the read/write side. This is what tells us, when a reader is
    * stuck, whether some other process still holds the write end open
    * (so EOF will never come) vs. nobody does (so the kernel must
    * have lost the EOF broadcast).
    *
    * Only pipes are decoded today; other handle types could grow a
    * similar fops-comparison helper if a future investigation calls
    * for it.
    */
   if (!is_kernel_thread(ti) && ti->pi) {
      for (int fd = 0; fd < KRN_MAX_HANDLES; fd++) {
         fs_handle h = ti->pi->handles[fd];
         bool is_write_end;
         struct pipe *pipe_p;

         if (!h)
            continue;

         pipe_p = debug_get_pipe_for_handle(h, &is_write_end);
         if (pipe_p)
            printk(NO_PREFIX "    fd=%d %s pipe(%p)\n",
                   fd, is_write_end ? "WR" : "RD", pipe_p);
      }
   }

   /*
    * If the task is parked on a kcond or kmutex, see if that pointer
    * matches one of the live pipes' conds/mutex; if so, dump that
    * pipe's bookkeeping and recent op history alongside the task line.
    * Indented for readability of the per-task block.
    */
   if (ti->wobj.type == WOBJ_KCOND || ti->wobj.type == WOBJ_KMUTEX) {
      void *p = wait_obj_get_ptr(&ti->wobj);
      if (p)
         debug_dump_pipe_state_for_obj(p);
   }
   return 0;
}

void debug_dump_all_tasks_state(void)
{
   disable_preemption();
   {
      printk("==== Task state dump (curr_tid=%d, switches=%llu) ====\n",
             get_curr_tid(),
             (unsigned long long)g_total_task_switches);

      iterate_over_tasks(&dump_task_state_cb, NULL);

      printk("==== End task dump ====\n");
   }
   enable_preemption();
}

#endif /* KRN_HANG_DETECTION */

static void sched_alive_thread(void *unused)
{
#if KRN_HANG_DETECTION
   u64 last_total_switches = 0;
#endif

   for (int counter = 0; ; counter++) {

      if (sched_alive_thread_enabled) {

#if KRN_HANG_DETECTION
         /*
          * Rich heartbeat: print the switch delta over the last
          * interval, and if it falls below the wedge threshold (after
          * the initial warm-up), dump every task's state. Then run the
          * existing self-test progress checks.
          */
         u64 cur = g_total_task_switches;
         u64 delta = cur - last_total_switches;
         last_total_switches = cur;

         printk("---- Sched alive thread: %d (switches: %llu) ----\n",
                counter, (unsigned long long)delta);

         if (KERNEL_SELFTESTS) {

            if (counter > SAT_WARMUP_INTERVALS &&
                delta < SAT_STUCK_SWITCH_THRESHOLD)
            {
               printk(
                  "[hang-detector] only %llu switches in last interval; "
                  "dumping task states\n",
                  (unsigned long long)delta
               );
               debug_dump_all_tasks_state();
            }

            if (counter % 2) {
               debug_check_for_deadlock();
               debug_check_for_any_progress();
            }
         }
#else
         /*
          * Plain heartbeat. Built without the hang detector — keep the
          * original "alive" trace and the self-test progress checks.
          */
         printk("---- Sched alive thread: %d ----\n", counter);

         if (KERNEL_SELFTESTS && (counter % 2)) {
            debug_check_for_deadlock();
            debug_check_for_any_progress();
         }
#endif
      }

#if KRN_HANG_DETECTION
      kernel_sleep(SAT_INTERVAL_TICKS);
#else
      kernel_sleep(KRN_TIMER_HZ);
#endif
   }
}

void init_extra_debug_features(void)
{
   if (kopt_sched_alive_thread)
      if (kthread_create(&sched_alive_thread, 0, NULL) < 0)
         panic("Unable to create a kthread for sched_alive_thread()");
}

int set_sched_alive_thread_enabled(bool enabled)
{
   sched_alive_thread_enabled = enabled;
   return 0;
}

void kmain_early_checks(void)
{
   if (KERNEL_FORCE_TC_ISYSTEM)
      panic("Builds with KERNEL_FORCE_TC_ISYSTEM=1 are not supposed to run");
}

#if SLOW_DEBUG_REF_COUNT

/*
 * Set here the address of the ref_count to track.
 */
void *debug_refcount_obj = (void *)0;

/* Return the new value */
int __retain_obj(atomic_int_t *ref_count)
{
   int ret = atomic_fetch_add(ref_count, 1) + 1;

   if (!debug_refcount_obj || ref_count == debug_refcount_obj) {
      printk(COLOR_GREEN "refcount at %p: %d -> %d" RESET_ATTRS "\n",
             ref_count, ret-1, ret);
   }

   return ret;
}

/* Return the new value */
int __release_obj(atomic_int_t *ref_count)
{
   int old = atomic_fetch_sub(ref_count, 1);
   int ret;
   ASSERT(old > 0);
   ret = old - 1;

   if (!debug_refcount_obj || ref_count == debug_refcount_obj) {
      printk(COLOR_RED "refcount at %p: %d -> %d" RESET_ATTRS "\n",
             ref_count, old, ret);
   }

   return ret;
}
#endif
