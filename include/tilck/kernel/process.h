/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/bintree.h>
#include <tilck/kernel/kmalloc.h>

#define USERMODE_VADDR_END (KERNEL_BASE_VA) /* biggest usermode vaddr + 1 */

#define MAX_BRK ((uptr)0x40000000)          // +1 GB

#define USER_MMAP_BEGIN MAX_BRK
#define USER_MMAP_END   ((uptr)0x80000000)  // +2 GB

#define KTHREAD_STACK_SIZE (PAGE_SIZE)
#define IO_COPYBUF_SIZE (PAGE_SIZE)
#define ARGS_COPYBUF_SIZE (PAGE_SIZE)
#define MAX_PID 32768
#define MAX_PATH 256

typedef enum {
   TASK_STATE_INVALID = 0,
   TASK_STATE_RUNNABLE = 1,
   TASK_STATE_RUNNING = 2,
   TASK_STATE_SLEEPING = 3,
   TASK_STATE_ZOMBIE = 4
} task_state_enum;

typedef struct {

   list_node list;

   fs_handle h;
   void *vaddr;
   size_t page_count;

} user_mapping;

struct process_info {

   int ref_count;

   int parent_pid;
   page_directory_t *pdir;
   void *brk;
   void *initial_brk;
   kmalloc_heap *mmap_heap;

   list_node children_list;

   char cwd[MAX_PATH]; /* current working directory */
   fs_handle handles[16]; /* for the moment, just a fixed-size small array */


   /*
    * TODO: when thread creation is implemented, use
    * copy_to_user() when writing to this address.
    */

   int *set_child_tid; /* NOTE: this is an user pointer */

   list_node mappings;
};

typedef struct process_info process_info;

struct task_info {

   bintree_node tree_by_tid_node;
   list_node runnable_node;
   list_node sleeping_node;
   list_node zombie_node;

   /*
    * NOTE: siblings_node is used ONLY for the main task in each process,
    * when tid == pid. For the other tasks (thread), this list is simply
    * ignored.
    */
   list_node siblings_node;

   int tid;   /* User/kernel task ID (pid in the Linux kernel) */
   u16 pid;   /*
               * ID of the owner process (tgid in the Linux kernel).
               * The main thread of each process has tid == pid
               */

   bool running_in_kernel;
   task_state_enum state;
   u32 exit_status;

   process_info *pi;

   u32 time_slot_ticks; /*
                         * ticks counter for the current time-slot: it's reset
                         * each time the task is selected by the scheduler.
                         */

   u64 total_ticks;
   u64 total_kernel_ticks;

   void *kernel_stack;
   void *io_copybuf;
   void *args_copybuf;

   wait_obj wobj;

   regs *state_regs;
   regs *fault_resume_regs;
   u32 faults_resume_mask;

   u64 ticks_before_wake_up;
   list_node wakeup_timer_node;

   /*
    * For kernel threads, this is a function pointer of the thread's entry
    * point. For user processes/threads, it is unused for the moment. In the
    * future, for processes it could be a path to the executable and for threads
    * still the entry-point.
    */
   void *what;

   arch_task_info_members arch; /* arch-specific fields */
};

typedef struct task_info task_info;

STATIC_ASSERT((sizeof(task_info) & ~POINTER_ALIGN_MASK) == 0);
STATIC_ASSERT((sizeof(process_info) & ~POINTER_ALIGN_MASK) == 0);

#ifdef __i386__
STATIC_ASSERT(OFFSET_OF(task_info, fault_resume_regs) == TI_F_RESUME_RS_OFF);
STATIC_ASSERT(OFFSET_OF(task_info, faults_resume_mask) == TI_FAULTS_MASK_OFF);
#endif

extern task_info *__current;
extern task_info *kernel_process;

extern list_node runnable_tasks_list;
extern list_node sleeping_tasks_list;
extern list_node zombie_tasks_list;

static ALWAYS_INLINE task_info *get_process_task(process_info *pi)
{
   /*
    * allocate_new_process() allocates task_info and process_info in one chunk
    * placing process_info immediately after task_info.
    */
   return ((task_info *)pi) - 1;
}

static ALWAYS_INLINE bool running_in_kernel(task_info *t)
{
   return t->running_in_kernel;
}

static ALWAYS_INLINE task_info *get_curr_task(void)
{
   return __current;
}

static ALWAYS_INLINE bool is_kernel_thread(task_info *ti)
{
   return ti->pid == 0;
}

user_mapping *
process_add_user_mapping(fs_handle h, void *vaddr, size_t page_count);
void process_remove_user_mapping(user_mapping *um);
user_mapping *process_get_user_mapping(void *vaddr);

void save_current_task_state(regs *);
void account_ticks(void);
bool need_reschedule(void);

NORETURN void switch_to_task(task_info *ti, int curr_irq);

void schedule(int curr_irq);
void schedule_outside_interrupt_context(void);
NORETURN void switch_to_idle_task(void);
NORETURN void switch_to_idle_task_outside_interrupt_context(void);

task_info *create_usermode_task(page_directory_t *pdir,
                                void *entry,
                                void *stack_addr,
                                task_info *task_to_use,
                                char *const *argv,
                                char *const *env);

void set_current_task_in_kernel(void);
void set_current_task_in_user_mode(void);

int create_new_pid(void);

task_info *get_task(int tid);

void task_info_reset_kernel_stack(task_info *ti);

void add_task(task_info *ti);
void remove_task(task_info *ti);
void task_change_state(task_info *ti, task_state_enum new_state);

void init_sched(void);
void create_kernel_process(void);
void init_task_lists(task_info *ti);
task_info *allocate_new_process(task_info *parent, int pid);
task_info *allocate_new_thread(process_info *pi);
void free_task(task_info *ti);
void free_mem_for_zombie_task(task_info *ti);
void arch_specific_new_task_setup(task_info *ti);
void arch_specific_free_task(task_info *ti);

typedef void (*kthread_func_ptr)();

NODISCARD task_info *kthread_create(kthread_func_ptr fun, void *arg);

// It is called when each kernel thread returns. May be called explicitly too.
void kthread_exit(void);

void kernel_sleep(u64 ticks);
void join_kernel_thread(int tid);

void task_set_wakeup_timer(task_info *task, u64 ticks);
void task_cancel_wakeup_timer(task_info *ti);


// TODO: consider moving these functions and the sched ones in sched.h

extern volatile u32 disable_preemption_count;

static ALWAYS_INLINE void disable_preemption(void)
{
   ATOMIC_ADD_AND_FETCH(&disable_preemption_count, 1);
}

static ALWAYS_INLINE void enable_preemption(void)
{
   u32 oldval = ATOMIC_FETCH_AND_SUB(&disable_preemption_count, 1);
   ASSERT(oldval > 0);
   (void)oldval;
}

#ifdef DEBUG

static ALWAYS_INLINE bool is_preemption_enabled(void)
{
   return disable_preemption_count == 0;
}

#endif

/*
 * Saves the current state and calls schedule().
 * That after, typically after some time, the scheduler will restore the thread
 * as if kernel_yield() returned and nothing else happened.
 */

void asm_kernel_yield(void);

/*
 * This wrapper is useful for adding ASSERTs and getting a backtrace containing
 * the caller's EIP in case of a failure.
 */
static inline void kernel_yield(void)
{
   ASSERT(is_preemption_enabled());
   asm_kernel_yield();
}

static ALWAYS_INLINE int thread_ti_to_tid(task_info *ti)
{
   ASSERT(ti->tid != ti->pid);
   return MAX_PID + (sptr) ((uptr)ti - KERNEL_BASE_VA);
}

static ALWAYS_INLINE task_info *thread_tid_to_ti(int tid)
{
   ASSERT(tid > MAX_PID);
   return (task_info *)((uptr)tid - MAX_PID + KERNEL_BASE_VA);
}

/* Internal stuff (used by process.c and process32.c) */
extern char *kernel_initial_stack[KERNEL_INITIAL_STACK_SIZE];
void switch_to_initial_kernel_stack(void);
static ALWAYS_INLINE void set_current_task(task_info *ti)
{
   __current = ti;
}
