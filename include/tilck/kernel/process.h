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

typedef struct {

   list_node node;

   fs_handle h;
   void *vaddr;
   size_t page_count;

} user_mapping;

typedef struct {

   bintree_node node;

   void *vaddr;
   size_t size;

} kernel_alloc;

struct process_info {

   int ref_count;

   int parent_pid;
   page_directory_t *pdir;
   void *brk;
   void *initial_brk;
   kmalloc_heap *mmap_heap;

   list children_list;        /* list of children processes (as task_info *) */
   list_node siblings_node;   /* nodes in parent's pi's children_list */

   char filepath[MAX_PATH]; /* executable's path */
   char cwd[MAX_PATH]; /* current working directory */
   fs_handle handles[MAX_HANDLES]; /* for now, just a small fixed-size array */

   __sighandler_t sa_handlers[_NSIG];
   uptr sa_mask[K_SIGACTION_MASK_WORDS];
   uptr sa_flags;

   void *proc_tty;

   /*
    * TODO: when thread creation is implemented, use
    * copy_to_user() when writing to this address.
    */

   int *set_child_tid; /* NOTE: this is an user pointer */

   list mappings;
};

typedef struct process_info process_info;

struct task_info {

   bintree_node tree_by_tid_node;
   list_node runnable_node;
   list_node sleeping_node;
   list_node zombie_node;

   list_node ignored_padding;

   int tid;   /* User/kernel task ID (pid in the Linux kernel) */
   u16 pid;   /*
               * ID of the owner process (tgid in the Linux kernel).
               * The main thread of each process has tid == pid
               */

   bool running_in_kernel;
   volatile ATOMIC(enum task_state) state;
   s32 exit_wstatus;

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

   ATOMIC(u32) ticks_before_wake_up;
   list_node wakeup_timer_node;

   /* A dedicated list for all the tasks waiting this task to end */
   list tasks_waiting_list;

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
   return ti->pid == 0;
}

static ALWAYS_INLINE int thread_ti_to_tid(task_info *ti)
{
   ASSERT(ti->tid != ti->pid);
   return (int)(MAX_PID + (sptr) ((uptr)ti - KERNEL_BASE_VA));
}

static ALWAYS_INLINE task_info *thread_tid_to_ti(int tid)
{
   ASSERT(tid > MAX_PID);
   return (task_info *)((uptr)tid - MAX_PID + KERNEL_BASE_VA);
}

static ALWAYS_INLINE bool is_tasklet_runner(task_info *ti)
{
   return ti->what == &tasklet_runner;
}

user_mapping *
process_add_user_mapping(fs_handle h, void *vaddr, size_t page_count);
void process_remove_user_mapping(user_mapping *um);
user_mapping *process_get_user_mapping(void *vaddr);

int setup_usermode_task(page_directory_t *pdir,
                         void *entry,
                         void *stack_addr,
                         task_info *task_to_use,
                         char *const *argv,
                         char *const *env,
                         task_info **ti_ref);

void set_current_task_in_kernel(void);
void set_current_task_in_user_mode(void);

task_info *allocate_new_process(task_info *parent, u16 pid);
task_info *allocate_new_thread(process_info *pi);
void free_task(task_info *ti);
void free_mem_for_zombie_task(task_info *ti);
bool arch_specific_new_task_setup(task_info *ti, task_info *parent);
void arch_specific_free_task(task_info *ti);
void wake_up_tasks_waiting_on(task_info *ti);
void init_process_lists(process_info *pi);

void *task_temp_kernel_alloc(size_t size);
void task_temp_kernel_free(void *ptr);

void terminate_process(task_info *ti, int exit_code, int term_sig);
void debug_show_task_list(void);
