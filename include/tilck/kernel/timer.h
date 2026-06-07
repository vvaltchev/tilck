/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck_gen_headers/config_sched.h>
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/bintree.h>
#include <tilck/kernel/list.h>

void kernel_sleep(u64 ticks);  /* sleep for `ticks` timer ticks (jiffies) */
void kernel_sleep_ms(u64 ms);  /* sleep for `ms` milliseconds */
void delay_us(u32 us);         /* busy-wait for `us` microseconds */

static ALWAYS_INLINE u64
ms_to_ticks(u64 ms)
{
   return ms / (1000 / KRN_TIMER_HZ);
}

u64 get_ticks(void);
void init_timer(void);

/*
 * ktimer: deadline-driven kernel timer object.
 *
 * Each task carries a primary_timer embedded in struct task; additional
 * timers (POSIX timer_create, kernel-internal deferred work, ...) are
 * heap-allocated and owned by the caller. Embedders typically recover
 * their wrapper struct from a ktimer * via CONTAINER_OF inside fire(),
 * but a generic ctx pointer is also passed in for callers that don't
 * want to wrap.
 *
 * Mode controls where fire() runs:
 *
 *   KTIMER_MODE_IRQ
 *      fire() is called from tick_all_timers() in timer-IRQ context,
 *      with interrupts disabled. Must be brief and IRQ-safe (no
 *      blocking, no allocation). This is the mode the primary timer
 *      uses to wake a sleeping task — the cheapest path.
 *
 *   KTIMER_MODE_DEFERRED
 *      tick_all_timers() chains the expired ktimer onto an internal
 *      list and wakes worker_threads[0]; fire() is then called from
 *      wth_process_single_job() in task context, with preemption
 *      disabled and interrupts enabled. Use when the callback is too
 *      heavy for IRQ context but still needs to run at the highest
 *      worker-thread priority (e.g. POSIX timer signal delivery).
 *
 * Cancellation: ktimer_cancel() returns true if the callback had not
 * yet fired (the timer was in the AVL tree or on the deferred-fire
 * list and has been removed). False means the callback is already
 * running or has completed — the caller must handle that race itself
 * (same contract as Linux hrtimer_cancel).
 */

enum ktimer_mode {
   KTIMER_MODE_IRQ,
   KTIMER_MODE_DEFERRED,
};

struct ktimer {

   struct bintree_node tree_node;
   u64 wakeup_at_tick;                /* 0 = not in the AVL tree */

   void (*fire)(struct ktimer *, void *ctx);
   void *ctx;
   enum ktimer_mode mode;

   /* Linkage on the deferred-fire list (KTIMER_MODE_DEFERRED only) */
   struct list_node deferred_node;
};

void ktimer_init(struct ktimer *t,
                 void (*fire)(struct ktimer *, void *ctx),
                 void *ctx,
                 enum ktimer_mode mode);

void ktimer_arm(struct ktimer *t, u64 ticks_from_now);
bool ktimer_arm_if_armed(struct ktimer *t, u64 new_ticks);
bool ktimer_cancel(struct ktimer *t);
bool ktimer_is_armed(struct ktimer *t);

/*
 * Drain pending KTIMER_MODE_DEFERRED callbacks. Called from
 * wth_process_single_job() before reading the ring buffer, only for
 * worker_threads[0].
 */
void run_pending_ktimers(void);

/*
 * True iff at least one KTIMER_MODE_DEFERRED fire is queued and
 * awaiting its callback. wth_run() consults this from inside the
 * IRQ-disabled "should I sleep?" block to close the race window
 * between an empty list, the rb-empty sleep decision, and an IRQ
 * that arrives in between.
 */
bool ktimer_has_pending_deferred(void);

/*
 * Fire callback for a task's embedded primary_timer (KTIMER_MODE_IRQ):
 * wakes the task from sleep. Exposed here so init_task_lists() can
 * pass it to ktimer_init() without depending on a timer-internal header.
 */
struct task;
void task_primary_timer_fire(struct ktimer *t, void *ctx);
