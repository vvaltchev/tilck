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
u32 __tick_duration;       /* integer ns per IRQ (the truncated value) */
u32 __tick_frac_per_tick;  /* residue numerator added to acc each IRQ */
u32 __tick_frac_denom;     /* denominator: when acc >= this, +1 ns */
u32 __tick_frac_acc;       /* running residue accumulator */
int __tick_adj_val;
int __tick_adj_ticks_rem;

/* Debug counters */
u32 slow_timer_irq_handler_count;

/* Temporary global used by asm_do_bogomips_loop() */
atomic_u32_t __bogo_loops;

/*
 * Timer tree: tasks are stored in an AVL tree ordered by absolute
 * expiry tick (with tid as tiebreaker for uniqueness). The cached
 * `earliest_timer` pointer makes the common per-tick check O(1).
 * Insert, cancel, and update are O(log N); the tree remove +
 * wakeup processing on expiry is O(log N) per expired timer, with
 * interrupts disabled for bounded time per iteration.
 */
static struct task *timer_tree_root;
static struct task *earliest_timer;

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

static long
timer_cmp(const void *a, const void *b)
{
   struct task *t1 = (struct task *)a;
   struct task *t2 = (struct task *)b;

   if (t1->wakeup_at_tick != t2->wakeup_at_tick)
      return t1->wakeup_at_tick < t2->wakeup_at_tick ? -1 : 1;

   return (long)t1->tid - (long)t2->tid;
}

void task_set_wakeup_timer(struct task *ti, u64 ticks)
{
   ulong var;
   ASSERT(ticks > 0);

   disable_interrupts(&var);
   {
      if (ti->wakeup_at_tick > 0) {
         bintree_remove(&timer_tree_root,
                        ti,
                        timer_cmp,
                        struct task,
                        timer_tree_node);
         bintree_node_init(&ti->timer_tree_node);
      }

      ti->wakeup_at_tick = __ticks + ticks;
      bintree_insert(&timer_tree_root,
                     ti,
                     timer_cmp,
                     struct task,
                     timer_tree_node);

      earliest_timer = bintree_get_first_obj(timer_tree_root,
                                             struct task,
                                             timer_tree_node);
   }
   enable_interrupts(&var);
}

void task_update_wakeup_timer_if_any(struct task *ti, u64 new_ticks)
{
   ulong var;
   ASSERT(new_ticks > 0);

   disable_interrupts(&var);
   {
      if (ti->wakeup_at_tick > 0) {

         bintree_remove(&timer_tree_root,
                        ti,
                        timer_cmp,
                        struct task,
                        timer_tree_node);
         bintree_node_init(&ti->timer_tree_node);

         ti->wakeup_at_tick = __ticks + new_ticks;
         bintree_insert(&timer_tree_root,
                        ti,
                        timer_cmp,
                        struct task,
                        timer_tree_node);

         earliest_timer = bintree_get_first_obj(timer_tree_root,
                                                struct task,
                                                timer_tree_node);
      }
   }
   enable_interrupts(&var);
}

u32 task_cancel_wakeup_timer(struct task *ti)
{
   ulong var;
   u32 remaining = 0;

   disable_interrupts(&var);
   {
      ti->timer_ready = false;

      if (ti->wakeup_at_tick > 0) {

         if (ti->wakeup_at_tick > __ticks)
            remaining = (u32)(ti->wakeup_at_tick - __ticks);

         bintree_remove(&timer_tree_root,
                        ti,
                        timer_cmp,
                        struct task,
                        timer_tree_node);
         bintree_node_init(&ti->timer_tree_node);

         if (earliest_timer == ti) {
            earliest_timer = bintree_get_first_obj(timer_tree_root,
                                                   struct task,
                                                   timer_tree_node);
         }

         ti->wakeup_at_tick = 0;
      }
   }
   enable_interrupts(&var);
   return remaining;
}

static void
tick_all_timers(void)
{
   struct task *pos;
   ulong var;
   bool any_woken = false;

   /* Fast path: no timer has expired — O(1), no IRQ-disable */
   if (!earliest_timer || earliest_timer->wakeup_at_tick > __ticks)
      return;

   while (true) {

      disable_interrupts(&var);
      {
         if (!earliest_timer || earliest_timer->wakeup_at_tick > __ticks) {
            enable_interrupts(&var);
            break;
         }

         pos = earliest_timer;
         pos->timer_ready = true;

         bintree_remove(&timer_tree_root,
                        pos,
                        timer_cmp,
                        struct task,
                        timer_tree_node);
         bintree_node_init(&pos->timer_tree_node);
         pos->wakeup_at_tick = 0;

         earliest_timer = bintree_get_first_obj(timer_tree_root,
                                                struct task,
                                                timer_tree_node);

         if (atomic_load(&pos->state) == TASK_STATE_SLEEPING) {

            /*
             * Stop-on-wake: see the matching comment in wake_up()
             * (kernel/wobj.c). A SIGSTOP delivered while pos was
             * sleeping left ti->stop_pending = true and the wait_obj
             * untouched; route the wake to STOPPED instead of
             * RUNNABLE and consume the flag.
             */
            enum task_state next = TASK_STATE_RUNNABLE;

            if (UNLIKELY(pos->stop_pending)) {
               next = TASK_STATE_STOPPED;
               pos->stop_pending = false;
            }

            if (next == TASK_STATE_RUNNABLE)
               wake_vruntime_handoff(pos);

            task_change_state_unsafe(pos, next);
            any_woken = true;
         }
      }
      enable_interrupts(&var);
   }

   if (any_woken)
      sched_set_need_resched();
}

void kernel_sleep(u64 ticks)
{
   struct task *curr = get_curr_task();

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
   ASSERT(are_interrupts_enabled());

   if (!ticks) {
      kernel_yield();
      return;
   }

   disable_preemption();
   task_change_state(curr, TASK_STATE_SLEEPING);
   task_set_wakeup_timer(curr, ticks);
   kernel_yield_preempt_disabled();
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

static enum irq_action timer_irq_handler(void *ctx)
{
   u32 ns_delta;
   ASSERT(are_interrupts_enabled());

   if (KRN_TRACK_NESTED_INTERR)
      if (timer_nested_irq())
         return IRQ_HANDLED;

   disable_interrupts_forced();
   {
      if (__tick_adj_ticks_rem) {
         ns_delta = (u32)((s32)__tick_duration + __tick_adj_val);
         __tick_adj_ticks_rem--;
      } else {
         ns_delta = __tick_duration;
      }

      __ticks++;
      __time_ns += ns_delta;

      /*
       * Fractional-ns accumulator: hw_timer_setup() gave us
       * (frac_per_tick, frac_denom) capturing the sub-ns residue
       * of the ideal tick interval. Add the residue each IRQ; when
       * the accumulator crosses the denominator, "spend" one ns of
       * residue by bumping __time_ns by 1 and subtracting. Net
       * effect: __time_ns advances at exactly (divisor / PIT_FREQ)
       * * TS_SCALE per IRQ on average, with no software-induced
       * drift from the truncation that just-using-__tick_duration
       * would accumulate.
       *
       * On arches that didn't bother filling in fractional info,
       * __tick_frac_per_tick is 0 and the if-branch never fires.
       */
      __tick_frac_acc += __tick_frac_per_tick;

      if (__tick_frac_acc >= __tick_frac_denom) {
         __tick_frac_acc -= __tick_frac_denom;
         __time_ns       += 1;
      }
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
      atomic_store(&__bogo_loops, 0);
      ctx->pass_start = true;
      return IRQ_NOT_HANDLED;
   }

   /* Successive IRQs */
   if (++ctx->ticks == MEASURE_BOGOMIPS_TICKS) {

      /* We're done */
      irq_uninstall_handler(X86_PC_TIMER_IRQ, &measure_bogomips);

      disable_interrupts_forced();
      {
         loops_per_tick =
            atomic_load(&__bogo_loops)
               * BOGOMIPS_CONST/MEASURE_BOGOMIPS_TICKS;
         loops_per_ms = loops_per_tick / (1000 / KRN_TIMER_HZ);
         loops_per_us = loops_per_ms / 1000;
         atomic_store(&__bogo_loops, (u32)-1);
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
   struct hw_timer_info info;

   measure_bogomips.context = &ctx;

   hw_timer_setup(TS_SCALE / KRN_TIMER_HZ, &info);
   __tick_duration      = info.ns_per_tick;
   __tick_frac_per_tick = info.frac_per_tick;
   __tick_frac_denom    = info.frac_denom;
   __tick_frac_acc      = 0;

   /*
    * Wire up the RTC Update-Ended interrupt now that workers and
    * IRQs are both initialized. UIE on the chip stays OFF until a
    * caller waits via rtc_wait_for_second_edge(); the install here
    * just gets the kcond and handler in place. No-op on arches
    * without UIE support (weak stub in kernel/misc.c).
    */
   init_rtc_uie();

   printk("*** Init the kernel timer\n");

   if (!wth_enqueue_anywhere(WTH_PRIO_HIGHEST, &do_bogomips_loop, &ctx))
      panic("Timer: unable to enqueue job in wth 0");

   irq_install_handler(X86_PC_TIMER_IRQ, &measure_bogomips);
   irq_install_handler(X86_PC_TIMER_IRQ, &timer);
}
