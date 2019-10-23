/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/bintree.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/signal.h>

STATIC_ASSERT((KERNEL_STACK_SIZE % PAGE_SIZE) == 0);
STATIC_ASSERT(((IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE) % PAGE_SIZE) == 0);

struct kernel_alloc {

   struct bintree_node node;
   void *vaddr;
   size_t size;
};

struct process {

   REF_COUNTED_OBJECT;

   int pid;                   /* process id (tgid in the Linux kernel) */
   int parent_pid;
   pdir_t *pdir;
   struct list_node siblings_node;   /* nodes in parent's pi's children_list */

   void *brk;
   void *initial_brk;
   struct kmalloc_heap *mmap_heap;

   struct list children_list;
   struct list mappings;

   void *proc_tty;
   bool did_call_execve;
   int *set_child_tid;                    /* NOTE: this is an user pointer */

   kmutex fslock;                         /* protects `handles` and `cwd` */
   mode_t umask;

   struct vfs_path cwd;                   /* CWD as a struct vfs_path */

   /* large members */

   char debug_filepath[64];               /* debug field: executable's path */
   char str_cwd[MAX_PATH];                /* current working directory */
   fs_handle handles[MAX_HANDLES];        /* just a small fixed-size array */

   __sighandler_t sa_handlers[_NSIG];
   uptr sa_mask[K_SIGACTION_MASK_WORDS];
   uptr sa_flags;
};

struct task {

   int tid;                 /* User/kernel task ID (pid in the Linux kernel) */

#ifdef BITS64

   /*
    * For the moment, `tid` has everywhere `int` as type, while the field is
    * used as key with the bintree_*_int functions which use pointer-sized
    * integers. Therefore, in case sizeof(sptr) > sizeof(int), we need some
    * padding.
    */
   int padding_0;
#endif

   struct process *pi;

   bool is_main_thread;     /* value of `tid == pi->pid` */
   bool running_in_kernel;

   volatile ATOMIC(enum task_state) state;

   regs_t *state_regs;
   regs_t *fault_resume_regs;
   u32 faults_resume_mask;

   struct bintree_node tree_by_tid_node;
   struct list_node runnable_node;
   struct list_node sleeping_node;
   struct list_node zombie_node;
   struct list_node wakeup_timer_node;

   struct list tasks_waiting_list; /* tasks waiting this task to end */

   s32 exit_wstatus;
   u32 time_slot_ticks; /*
                         * ticks counter for the current time-slot: it's reset
                         * each time the task is selected by the scheduler.
                         */

   u64 total_ticks;
   u64 total_kernel_ticks;

   void *kernel_stack;
   void *io_copybuf;
   void *args_copybuf;

   struct wait_obj wobj;

   ATOMIC(u32) ticks_before_wake_up;

   /* Temp kernel allocations for user requests */
   struct kernel_alloc *kallocs_tree_root;

   /*
    * For kernel threads, this is a function pointer of the thread's entry
    * point. For user processes/threads, it is unused for the moment. In the
    * future, for processes it could be a path to the executable and for threads
    * still the entry-point.
    */
   void *what;

   arch_task_members_t arch; /* arch-specific fields */
};

STATIC_ASSERT((sizeof(struct task) & ~POINTER_ALIGN_MASK) == 0);
STATIC_ASSERT((sizeof(struct process) & ~POINTER_ALIGN_MASK) == 0);

#ifdef __i386__
STATIC_ASSERT(
   OFFSET_OF(struct task, fault_resume_regs) == TI_F_RESUME_RS_OFF
);
STATIC_ASSERT(
   OFFSET_OF(struct task, faults_resume_mask) == TI_FAULTS_MASK_OFF
);
#endif

static ALWAYS_INLINE struct task *
get_process_task(struct process *pi)
{
   /*
    * allocate_new_process() allocates `struct task` and `struct process` in one
    * chunk placing struct process immediately after struct task.
    */
   return ((struct task *)pi) - 1;
}

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

static ALWAYS_INLINE int kthread_calc_tid(struct task *ti)
{
   ASSERT(is_kernel_thread(ti));
   return (int)(MAX_PID + (sptr) ((uptr)ti - KERNEL_BASE_VA));
}

static ALWAYS_INLINE struct task *kthread_get_ptr(int tid)
{
   ASSERT(tid > MAX_PID);
   return (struct task *)((uptr)tid - MAX_PID + KERNEL_BASE_VA);
}

static ALWAYS_INLINE bool is_tasklet_runner(struct task *ti)
{
   return ti->what == &tasklet_runner;
}

int first_execve(const char *abs_path, const char *const *argv);
int setup_usermode_task(pdir_t *pdir,
                        void *entry,
                        void *stack_addr,
                        struct task *task_to_use,
                        const char *const *argv,
                        const char *const *env,
                        struct task **ti_ref);

void set_current_task_in_kernel(void);
void set_current_task_in_user_mode(void);

struct task *allocate_new_process(struct task *parent, int pid);
struct task *allocate_new_thread(struct process *pi);
void free_task(struct task *ti);
void free_mem_for_zombie_task(struct task *ti);
bool arch_specific_new_task_setup(struct task *ti, struct task *parent);
void arch_specific_free_task(struct task *ti);
void wake_up_tasks_waiting_on(struct task *ti);
void init_process_lists(struct process *pi);

void *task_temp_kernel_alloc(size_t size);
void task_temp_kernel_free(void *ptr);

void process_set_cwd2_nolock(struct vfs_path *tp);
void process_set_cwd2_nolock_raw(struct process *pi, struct vfs_path *tp);
void terminate_process(struct task *ti, int exit_code, int term_sig);
