/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>
#include <tilck/kernel/hal_types.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/bintree.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/tasklet.h>

#define TIME_SLOT_TICKS (TIMER_HZ / 20)

enum task_state {
   TASK_STATE_INVALID   = 0,
   TASK_STATE_RUNNABLE  = 1,
   TASK_STATE_RUNNING   = 2,
   TASK_STATE_SLEEPING  = 3,
   TASK_STATE_ZOMBIE    = 4
};

enum wakeup_reason {
   task_died,
   task_stopped,
   task_continued,
};

struct misc_buf {
   char path_buf[MAX_PATH];
   char unused[1024 - MAX_PATH];
   char execve_ctx[1024];
   char resolve_ctx[2048];
};

struct task {

   union {

      int tid;               /* User/kernel task ID (pid in the Linux kernel) */

      /*
       * For the moment, `tid` has everywhere `int` as type, while the field is
       * used as key with the bintree_*_int functions which use pointer-sized
       * integers. Therefore, in case sizeof(long) > sizeof(int), we need some
       * padding.
       */

      ulong padding_0;
   };

   struct process *pi;

   bool is_main_thread;     /* value of `tid == pi->pid` */
   bool running_in_kernel;
   bool stopped;
   bool was_stopped;

   /*
    * Technically `state` is just 1 byte wide and should be enough on all the
    * architectures to guarantee its simple atomicity (not sequential
    * consistency!!), the only thing we need in non-SMP kernels. BUT, it is
    * marked as ATOMIC here for consistency, as in interrupt context this member
    * might be checked and changed (see the tasklet subsystem).
    *
    * If, in the future `state` will need to become wider than just 1 single
    * byte, then even getting a plain atomicity would require ATOMIC(x) on some
    * architectures, while on i386 and x86_64 it won't make a difference.
    * Therefore, marking it ATOMIC at the moment is more about semantic than
    * anything else, but in the future it will be actually needed.
    *
    * NOTE: the following implications are *not* true:
    *
    *    volatile -> atomic
    *    atomic -> volatile
    *
    * The field needs to be also volatile because its value is read in loops
    * expecting it to change as some point (see sys_waitpid()). Theoretically,
    * in case of consecutive atomic loads, the compiler is _not_ obliged to
    * do every time an actual read and it might cache the value in a register,
    * according to the C11 atomic model. In practice with GCC this can happen
    * only with relaxed atomics (the ones used in Tilck), at best of my
    * knowledge, but it is still good write C11 compliant code, instead of
    * relying on the current behavior.
    */
   volatile ATOMIC(enum task_state) state;

   regs_t *state_regs;
   regs_t *fault_resume_regs;
   u32 faults_resume_mask;
   ATOMIC(int) pending_signal;
   void *tasklet_thread;              /* used only for tasklet runner threads */

   struct bintree_node tree_by_tid_node;
   struct list_node runnable_node;
   struct list_node sleeping_node;
   struct list_node zombie_node;
   struct list_node wakeup_timer_node;
   struct list_node siblings_node;    /* nodes in parent's pi's children list */

   struct list tasks_waiting_list;    /* tasks waiting this task to end */

   s32 wstatus;                       /* waitpid's wstatus */

   u32 time_slot_ticks; /*
                         * ticks counter for the current time-slot: it's reset
                         * each time the task is selected by the scheduler.
                         */

   u64 total_ticks;
   u64 total_kernel_ticks;

   void *kernel_stack;
   void *args_copybuf;

   union {
      void *io_copybuf;
      struct misc_buf *misc_buf;
   };

   struct wait_obj wobj;
   u32 ticks_before_wake_up;

   /* Temp kernel allocations for user requests */
   struct kernel_alloc *kallocs_tree_root;

   /* This task is stopped because of its vfork-ed child */
   bool vfork_stopped;

   /* Trace the syscalls of this task (requires debugpanel) */
   bool traced;

   /* The task was sleeping on a timer and has just been woken up */
   bool timer_ready;

   /*
    * For kernel threads, this is a function pointer of the thread's entry
    * point. For user processes/threads, it is unused for the moment. In the
    * future, for processes it could be a path to the executable and for threads
    * still the entry-point.
    */
   void *what;

   /* See the comment above struct process' arch_fields */
   char arch_fields[ARCH_TASK_MEMBERS_SIZE] ALIGNED_AT(ARCH_TASK_MEMBERS_ALIGN);
};

extern struct task *kernel_process;
extern struct process *kernel_process_pi;

extern struct list runnable_tasks_list;
extern struct list sleeping_tasks_list;
extern struct list zombie_tasks_list;

extern ATOMIC(u32) disable_preemption_count;

#define KTH_ALLOC_BUFS                       (1 << 0)
#define KERNEL_TID_START                        10000
#define KERNEL_MAX_TID                           1024 /* + KERNEL_TID_START */

STATIC_ASSERT(MAX_PID < KERNEL_TID_START);

void init_sched(void);
struct task *get_task(int tid);
struct process *get_process(int pid);
void task_change_state(struct task *ti, enum task_state new_state);

static ALWAYS_INLINE void sched_set_need_resched(void)
{
   extern ATOMIC(bool) need_resched;
   atomic_store_explicit(&need_resched, true, mo_relaxed);
}

static ALWAYS_INLINE void sched_clear_need_resched(void)
{
   extern ATOMIC(bool) need_resched;
   atomic_store_explicit(&need_resched, false, mo_relaxed);
}

static ALWAYS_INLINE bool need_reschedule(void)
{
   extern ATOMIC(bool) need_resched;
   return atomic_load_explicit(&need_resched, mo_relaxed);
}

static ALWAYS_INLINE void disable_preemption(void)
{
   atomic_fetch_add_explicit(&disable_preemption_count, 1U, mo_relaxed);
}

static ALWAYS_INLINE void enable_preemption(void)
{
   DEBUG_ONLY_UNSAFE(u32 oldval =)
   atomic_fetch_sub_explicit(&disable_preemption_count, 1U, mo_relaxed);

   ASSERT(oldval > 0);
}

/*
 * WARNING: this function is dangerous and should NEVER be used it for anything
 * other than special self-test code paths. See selftest_kmutex_ord_med().
 */
static ALWAYS_INLINE void force_enable_preemption(void)
{
   atomic_store_explicit(&disable_preemption_count, 0u, mo_relaxed);
}

#ifdef DEBUG

   /*
    * This function is supposed to be used only in ASSERTs or in special debug
    * only pieces of code. No "regular" code should use is_preemption_enabled()
    * to change its behavior.
    */
   static ALWAYS_INLINE bool is_preemption_enabled(void)
   {
      return disable_preemption_count == 0;
   }

#endif

static ALWAYS_INLINE bool running_in_kernel(struct task *t)
{
   return t->running_in_kernel;
}

static ALWAYS_INLINE bool is_kernel_thread(struct task *ti)
{
   return ti->pi == kernel_process_pi;
}

static ALWAYS_INLINE bool is_main_thread(struct task *ti)
{
   return ti->is_main_thread;
}

static ALWAYS_INLINE bool is_tasklet_runner(struct task *ti)
{
   return ti->what == &tasklet_runner;
}

/*
 * This wrapper is useful for adding ASSERTs and getting a backtrace containing
 * the caller's EIP in case of a failure.
 */
static ALWAYS_INLINE bool kernel_yield(void)
{
   /*
    * Saves the current state and calls schedule().
    * That after, typically after some time, the scheduler will restore the
    * thread as if kernel_yield() returned and nothing else happened.
    */
   extern bool asm_kernel_yield(void);


   ASSERT(is_preemption_enabled());
   return asm_kernel_yield();
}

static ALWAYS_INLINE struct task *get_curr_task(void)
{
   extern struct task *__current;

   /*
    * Access to `__current` DOES NOT need to be atomic (not even relaxed) even
    * on architectures (!= x86) where loading/storing a pointer-size integer
    * requires more than one instruction, for the following reasons:
    *
    *    - While ANY given task is running, `__current` is always set and valid.
    *      That is true even if the task is preempted after reading for example
    *      only half of its value and than its execution resumed back, because
    *      during the task switch the older value of `__current` will be
    *      restored.
    *
    *    - The `__current` variable is set only in three cases:
    *       - during initialization [create_kernel_process()]
    *       - in switch_to_task() [with interrupts disabled]
    *       - in kthread_exit() [with interrupts disabled]
    */
   return __current;
}

/* Hack: it works only if the C file includes process.h, but that's fine. */
#define get_curr_proc() (get_curr_task()->pi)

static ALWAYS_INLINE enum task_state
get_curr_task_state(void)
{
   STATIC_ASSERT(sizeof(get_curr_task()->state) == 4);

   /*
    * Casting `state` to u32 and back to `enum task_state` to avoid compiler
    * errors in some weird configurations.
    */

   return (enum task_state) atomic_load_explicit(
      (ATOMIC(u32)*)&get_curr_task()->state,
      mo_relaxed
   );
}

static ALWAYS_INLINE bool pending_signals(void)
{
   struct task *curr = get_curr_task();
   int sig = atomic_load_explicit(&curr->pending_signal, mo_relaxed);
   return sig != 0;
}

NORETURN void switch_to_task(struct task *ti);

void schedule(void);
int get_curr_tid(void);
int get_curr_pid(void);
void save_current_task_state(regs_t *);
void account_ticks(void);
int create_new_pid(void);
int create_new_kernel_tid(void);
void task_info_reset_kernel_stack(struct task *ti);
void add_task(struct task *ti);
void remove_task(struct task *ti);
void create_kernel_process(void);
void init_task_lists(struct task *ti);

// It is called when each kernel thread returns. May be called explicitly too.
void kthread_exit(void);

void kernel_sleep(u64 ticks);
void kthread_join(int tid);
void kthread_join_all(const int *tids, size_t n);

void task_set_wakeup_timer(struct task *task, u32 ticks);
void task_update_wakeup_timer_if_any(struct task *ti, u32 new_ticks);
u32 task_cancel_wakeup_timer(struct task *ti);

typedef void (*kthread_func_ptr)();
NODISCARD int kthread_create(kthread_func_ptr fun, int fl, void *arg);
int iterate_over_tasks(bintree_visit_cb func, void *arg);
int sched_count_proc_in_group(int pgid);
int sched_get_session_of_group(int pgid);

struct process *task_get_pi_opaque(struct task *ti);
void process_set_tty(struct process *pi, void *t);
bool in_currently_dying_task(void);

void set_current_task_in_kernel(void);
void set_current_task_in_user_mode(void);
void *task_temp_kernel_alloc(size_t size);
void task_temp_kernel_free(void *ptr);
