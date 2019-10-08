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

struct user_mapping {

   list_node node;

   fs_handle h;
   size_t len;
   size_t off;

   union {
      void *vaddrp;
      uptr vaddr;
   };

   int prot;

};

typedef struct user_mapping user_mapping;

typedef struct {

   bintree_node node;

   void *vaddr;
   size_t size;

} kernel_alloc;

struct process_info {

   REF_COUNTED_OBJECT;

   int pid;                   /* process id (tgid in the Linux kernel) */
   int parent_pid;
   pdir_t *pdir;
   list_node siblings_node;   /* nodes in parent's pi's children_list */

   void *brk;
   void *initial_brk;
   kmalloc_heap *mmap_heap;

   list children_list;
   list mappings;

   void *proc_tty;
   bool did_call_execve;
   int *set_child_tid;                 /* NOTE: this is an user pointer */

   kmutex fslock;                      /* protects `handles` and `cwd` */
   mode_t umask;

   vfs_path cwd;                       /* CWD as a vfs_path */

   /* large members */

   char debug_filepath[64];               /* debug field: executable's path */
   char str_cwd[MAX_PATH];                /* current working directory */
   fs_handle handles[MAX_HANDLES];        /* just a small fixed-size array */

   __sighandler_t sa_handlers[_NSIG];
   uptr sa_mask[K_SIGACTION_MASK_WORDS];
   uptr sa_flags;
};

typedef struct process_info process_info;

struct task_info {

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

   process_info *pi;

   bool is_main_thread;     /* value of `tid == pi->pid` */
   bool running_in_kernel;

   volatile ATOMIC(enum task_state) state;

   regs *state_regs;
   regs *fault_resume_regs;
   u32 faults_resume_mask;

   bintree_node tree_by_tid_node;
   list_node runnable_node;
   list_node sleeping_node;
   list_node zombie_node;
   list_node wakeup_timer_node;

   list tasks_waiting_list; /* tasks waiting this task to end */

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

   wait_obj wobj;

   ATOMIC(u32) ticks_before_wake_up;

   /* Temp kernel allocations for user requests */
   kernel_alloc *kallocs_tree_root;

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

static ALWAYS_INLINE bool is_kernel_thread(task_info *ti)
{
   return ti->pi == kernel_process_pi;
}

static ALWAYS_INLINE bool is_main_thread(task_info *ti)
{
   return ti->is_main_thread;
}

static ALWAYS_INLINE int kthread_calc_tid(task_info *ti)
{
   ASSERT(is_kernel_thread(ti));
   return (int)(MAX_PID + (sptr) ((uptr)ti - KERNEL_BASE_VA));
}

static ALWAYS_INLINE task_info *kthread_get_ptr(int tid)
{
   ASSERT(tid > MAX_PID);
   return (task_info *)((uptr)tid - MAX_PID + KERNEL_BASE_VA);
}

static ALWAYS_INLINE bool is_tasklet_runner(task_info *ti)
{
   return ti->what == &tasklet_runner;
}

user_mapping *
process_add_user_mapping(fs_handle h, void *v, size_t ln, size_t off, int prot);
void process_remove_user_mapping(user_mapping *um);
void full_remove_user_mapping(process_info *pi, user_mapping *um);
void remove_all_mappings_of_handle(process_info *pi, fs_handle h);
user_mapping *process_get_user_mapping(void *vaddr);

int first_execve(const char *abs_path, const char *const *argv);
int setup_usermode_task(pdir_t *pdir,
                        void *entry,
                        void *stack_addr,
                        task_info *task_to_use,
                        const char *const *argv,
                        const char *const *env,
                        task_info **ti_ref);

void set_current_task_in_kernel(void);
void set_current_task_in_user_mode(void);

task_info *allocate_new_process(task_info *parent, int pid);
task_info *allocate_new_thread(process_info *pi);
void free_task(task_info *ti);
void free_mem_for_zombie_task(task_info *ti);
bool arch_specific_new_task_setup(task_info *ti, task_info *parent);
void arch_specific_free_task(task_info *ti);
void wake_up_tasks_waiting_on(task_info *ti);
void init_process_lists(process_info *pi);

void *task_temp_kernel_alloc(size_t size);
void task_temp_kernel_free(void *ptr);

void process_set_cwd2_nolock(vfs_path *tp);
void process_set_cwd2_nolock_raw(process_info *pi, vfs_path *tp);
void terminate_process(task_info *ti, int exit_code, int term_sig);
