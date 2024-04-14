/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/datetime.h>

FASTCALL void asm_nop_loop(u32 iters);

/* Jiffies */
static u64 __ticks;        /* ticks since the timer started */

/* System time */
u64 __time_ns;             /* nanoseconds since the timer started */
u32 __tick_duration;       /* the real duration of a tick, ~TS_SCALE/TIMER_HZ */
int __tick_adj_val;
int __tick_adj_ticks_rem;

/* Debug counters */
u32 slow_timer_irq_handler_count;

/* Temporary global used by asm_do_bogomips_loop() */
volatile ATOMIC(u32) __bogo_loops;

/* Static variables */
static struct list timer_wakeup_list = STATIC_LIST_INIT(timer_wakeup_list);
static u32 loops_per_tick;         /* Tilck bogoMips as loops/tick    */
static u32 loops_per_ms = 5000000; /* loops/millisecond (initial val)  */
static u32 loops_per_us = 5000;    /* loops/microsecond (initial val) */

u64 get_ticks(void)
{
   u64 curr_ticks;
   ulong var;

   disable_interrupts(&var);
   {
      curr_ticks = __ticks;
   }
   enable_interrupts(&var);
   return curr_ticks;
}

void task_set_wakeup_timer(struct task *ti, u32 ticks)
{
   ulong var;
   ASSERT(ticks > 0);

   disable_interrupts(&var);
   {
      if (ti->ticks_before_wake_up == 0) {
         ASSERT(!list_is_node_in_list(&ti->wakeup_timer_node));
         list_add_tail(&timer_wakeup_list, &ti->wakeup_timer_node);
      } else {
         ASSERT(list_is_node_in_list(&ti->wakeup_timer_node));
      }

      ti->ticks_before_wake_up = ticks;
   }
   enable_interrupts(&var);
}

void task_update_wakeup_timer_if_any(struct task *ti, u32 new_ticks)
{
   ulong var;
   ASSERT(new_ticks > 0);

   disable_interrupts(&var);
   {
      if (ti->ticks_before_wake_up > 0) {
         ASSERT(list_is_node_in_list(&ti->wakeup_timer_node));
         ti->ticks_before_wake_up = new_ticks;
      }
   }
   enable_interrupts(&var);
}

u32 task_cancel_wakeup_timer(struct task *ti)
{
   ulong var;
   u32 old;
   disable_interrupts(&var);
   {
      old = ti->ticks_before_wake_up;

      if (old > 0) {
         ti->timer_ready = false;
         ti->ticks_before_wake_up = 0;
         list_remove(&ti->wakeup_timer_node);
      }
   }
   enable_interrupts(&var);
   return old;
}

static void tick_all_timers(void)
{
   struct task *pos, *temp;
   bool any_woken_up_task = false;
   ulong var;

   /*
    * This is *NOT* the best we can do. In particular, it's terrible to keep
    * the interrupts disabled while iterating the _whole_ timer_wakeup_list.
    *
    * Possible better solutions
    * -----------------------------
    * 1. Keep the tasks to wake-up in a sort of ordered list and then use
    * relative timers. This way, at each tick we'll have to decrement just one
    * single counter. We'll start decrement the next counter only when the first
    * counter reaches 0 and its list node is removed. Of course, if we cannot
    * use kmalloc() in case of sleep, it gets much harder to create such an
    * ordered list and make it live inside a member of struct task. Maybe a BST
    * will do the job, but that would require paying O(logN) per tick for
    * finding the earliest timer. Not sure how better would be now for N < 50
    * (typical), given the huge added constant for the BST functions. Also, the
    * cancellation of a timer would require some extra effort in order to
    * re-calculate the relative timer values, while we want the cancellation to
    * be light-fast because it's run by IRQ handlers.
    *
    * 2. Use a fixed number of wakeup lists like: short-term, mid-term and
    * long-term. Current task's wakeup list node will be placed in one those
    * lists, depending on far in the future the task is supposted to be woke up.
    * On every tick, here in tick_all_timers(), ONLY the short-term list will be
    * iterated. That's a considerable improvement. In a system, there might be
    * even 1,000 active timers, but how many of them will expire in the next
    * second? Only a small percentage of them. To further improve the
    * scalability, it's possible to have even more than 3 lists, or to further
    * reduce the time-horizon of the short-term list. Periodically, but NOT on
    * every tick, the `ticks_before_wake_up` field belonging to tasks in the
    * other wakeup lists will adjusted with bigger decrements and tasks will be
    * moved from one list to another. That will also happen in case the wakeup
    * timer is changed for task with an already active timer. This solution
    * looks much better than solution 1.
    *
    * Conclusion
    * ---------------------
    * For the moment, given the very limited scale of Tilck (tens of tasks at
    * most running on the whole system), the current solution is safe and
    * good-enough but, at some point, a smarter ad-hoc solution should be
    * devised. Probably solution 2 is the right candidate.
    */
   disable_interrupts(&var);

   list_for_each(pos, temp, &timer_wakeup_list, wakeup_timer_node) {

      /* If task is part of this list, it's counter must be > 0 */
      ASSERT(pos->ticks_before_wake_up > 0);

      if (UNLIKELY(--pos->ticks_before_wake_up == 0)) {

         pos->timer_ready = true;
         list_remove(&pos->wakeup_timer_node);

         if (pos->state == TASK_STATE_SLEEPING) {
            task_change_state(pos, TASK_STATE_RUNNABLE);
            any_woken_up_task = true;
         }
      }
   }

   enable_interrupts(&var);

   if (any_woken_up_task)
      sched_set_need_resched();
}

static void do_sleep_internal(u32 ticks)
{
   ASSERT(are_interrupts_enabled());

   disable_preemption();
   task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
   task_set_wakeup_timer(get_curr_task(), ticks);
   kernel_yield_preempt_disabled();
}

void kernel_sleep(u64 ticks)
{
   if (in_panic()) {

      /*
       * Typically, we don't sleep in panic of course but, if the kopt_panic_kb
       * is enabled we'll run a mini interactive debugger, which might run the
       * debug panel itself, which calls kernel_sleep(). Because don't really
       * have a scheduler nor a timer anymore, just fake it by returning
       * immediately. All the code after panic(), run under special conditions
       * where every hack is allowed :-)
       */
      return;
   }

   DEBUG_ONLY(check_not_in_irq_handler());

   /*
    * Implementation: why
    * ---------------------
    *
    * In theory, the function could be implemented just as:
    *
    *    if (ticks) {
    *       task_set_wakeup_timer(get_curr_task(), ticks);
    *       task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
    *    }
    *    kernel_yield();
    *
    * But that would require task->ticks_before_wake_up to be actually 64-bit,
    * wide and that's bad on 32-bit systems because:
    *
    *    - it would require using the soft 64-bit integers (slow)
    *    - it would make impossible, in the case we wanted that, the counter
    *      to be atomic.
    *
    * Therefore, in order to use a 32-bit value for 'ticks_before_wake_up' and,
    * at the same time being able to sleep for more than 2^32-1 ticks, we need
    * a more tricky implementation (below), and the little extra runtime price
    * for it is totally fine, since we're going to sleep anyways!
    *
    * Implementation: how
    * ----------------------
    *
    * The simpler way to explain the algorithm is to just assume everything
    * is in base 10 and that ticks_before_wake_up has 2 digits, while we want
    * to support 4 digits sleep time. For example, we want to sleep for 234
    * ticks. The algorithm first computes 534 % 100 = 34 and then 534 / 100 = 5.
    * After that, it sleeps q (= 5) times for 99 ticks (max allowed). Clearly,
    * we missed 5 ticks (5 * 99 < 500) this way, but we'll going to fix that
    * buy just sleeping 'q' ticks. Thus, by now, we've slept for 500 ticks.
    * Now we have to sleep for 34 ticks more are we're done.
    *
    * The same logic applies to base-2 case with 32-bit and 64-bit integers,
    * just the numbers are much bigger. The remainder can be computed using
    * a bitmask, while the division by using just a right shift.
    */

   const u32 rem = ticks & 0xffffffff;
   const u32 q = ticks >> 32;

   if (q) {

      for (u32 i = 0; i < q; i++) {

         do_sleep_internal(0xffffffff);

         if (pending_signals())
            return;
      }

      do_sleep_internal(q);

      if (pending_signals())
         return;
   }

   if (rem) {
      do_sleep_internal(rem);
   }

   if (!q && !rem)
      kernel_yield();
}

void kernel_sleep_ms(u64 ms)
{
   kernel_sleep(MAX(1u, ms_to_ticks(ms)));
}

static ALWAYS_INLINE bool timer_nested_irq(void)
{
   bool res = false;

   if (KRN_TRACK_NESTED_INTERR) {

      ulong var;
      disable_interrupts(&var);

      if (in_nested_irq_num(X86_PC_TIMER_IRQ)) {
         slow_timer_irq_handler_count++;
         res = true;
      }

      enable_interrupts(&var);
   }

   return res;
}

#include <tilck/kernel/process.h>
static enum irq_action timer_irq_handler(void *ctx)
{
   u32 ns_delta;
   ASSERT(are_interrupts_enabled());

   if (KRN_TRACK_NESTED_INTERR)
      if (timer_nested_irq())
         return IRQ_HANDLED;

   // disable_interrupts_forced();
   // {
   //    struct task *pos;
   //    list_for_each_ro(pos, &runnable_tasks_list, runnable_node) {

   //       if (pos->state != TASK_STATE_ZOMBIE)
   //          debug_validate_resume_ip_task(pos);
   //    }
   // }
   // enable_interrupts_forced();

   /*
    * Compute `ns_delta` by reading `__tick_duration` and `__tick_adj_val` here
    * without disabling interrupts, because it's safe to do so. Also, decrement
    * `__tick_adj_ticks_rem` too. Why it's safe:
    *
    *    1. `__tick_duration` is immutable
    *    2. `__tick_adj_val` is changed only by datetime.c while keeping
    *       interrupts disabled and it's read only here. Nested timer IRQs
    *       will be ignored (see above). No other IRQ handler should read it.
    */

   if (__tick_adj_ticks_rem) {
      ns_delta = (u32)((s32)__tick_duration + __tick_adj_val);
      __tick_adj_ticks_rem--;
   } else {
      ns_delta = __tick_duration;
   }

   disable_interrupts_forced();
   {
      /*
       * Alter __ticks and __time_ns here, while keeping the interrupts disabled
       * because other IRQ handlers might need to use them. While, as explained
       * above, `__tick_adj_val` and `__tick_adj_ticks_rem` will never need to
       * be read or written by IRQ handlers.
       */
      __ticks++;
      __time_ns += ns_delta;
   }
   enable_interrupts_forced();

   sched_account_ticks();
   tick_all_timers();
   return IRQ_HANDLED;
}

static enum irq_action measure_bogomips_irq_handler(void *ctx);

DEFINE_IRQ_HANDLER_NODE(timer, timer_irq_handler, NULL);
DEFINE_IRQ_HANDLER_NODE(measure_bogomips, measure_bogomips_irq_handler, NULL);

struct bogo_measure_ctx {
   bool started;
   bool pass_start;
   u32 ticks;
};

static enum irq_action measure_bogomips_irq_handler(void *arg)
{
   struct bogo_measure_ctx *ctx = arg;

   if (!ctx->started)
      return IRQ_NOT_HANDLED;

   if (UNLIKELY(!ctx->pass_start)) {

      /*
       * First IRQ: reset the loops value.
       *
       * Reason: the asm_do_bogomips_loop() has been spinning already when we
       * received this IRQ, but we cannot be sure that it started spinning
       * immediately after a tick. In order to increase the precision, just
       * discard the loops so far in this partial tick and start counting zero
       * from now, when the timer IRQ just arrived.
       */
      __bogo_loops = 0;
      ctx->pass_start = true;
      return IRQ_NOT_HANDLED;
   }

   /* Successive IRQs */
   if (++ctx->ticks == MEASURE_BOGOMIPS_TICKS) {

      /* We're done */
      irq_uninstall_handler(X86_PC_TIMER_IRQ, &measure_bogomips);

      disable_interrupts_forced();
      {
         loops_per_tick = __bogo_loops * BOGOMIPS_CONST/MEASURE_BOGOMIPS_TICKS;
         loops_per_ms = loops_per_tick / (1000 / TIMER_HZ);
         loops_per_us = loops_per_ms / 1000;
         __bogo_loops = -1;
      }
      enable_interrupts_forced();
   }

   return IRQ_NOT_HANDLED;   /* always allow the real IRQ handler to go */
}

static void do_bogomips_loop(void *arg)
{
   void asm_do_bogomips_loop(void);
   struct bogo_measure_ctx *ctx = arg;

   ASSERT(is_preemption_enabled());
   disable_preemption();
   {
      ctx->started = true;
      asm_do_bogomips_loop();
   }
   enable_preemption();
   printk("Tilck bogoMips: %u.%03u\n", loops_per_us, loops_per_ms % 1000);
}

void delay_us(u32 us)
{
   u32 loops;
   ASSERT(us <= 100 * 1000);

   if (LIKELY(loops_per_us >= 10))
      loops = us * loops_per_us;
   else
      loops = (us * loops_per_ms) / 1000;

   if (LIKELY(loops > 0))
      asm_nop_loop(loops);
}

void init_timer(void)
{
   static struct bogo_measure_ctx ctx;
   measure_bogomips.context = &ctx;

   __tick_duration = hw_timer_setup(TS_SCALE / TIMER_HZ);

   printk("*** Init the kernel timer\n");

   if (!wth_enqueue_anywhere(WTH_PRIO_HIGHEST, &do_bogomips_loop, &ctx))
      panic("Timer: unable to enqueue job in wth 0");

   irq_install_handler(X86_PC_TIMER_IRQ, &measure_bogomips);
   irq_install_handler(X86_PC_TIMER_IRQ, &timer);
}
