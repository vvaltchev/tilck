/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/bintree.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/hal_types.h>
#include <tilck/kernel/elf_loader.h>
#include <tilck/kernel/fs/vfs_base.h>
#include <tilck/kernel/fs/flock.h>
#include <tilck/kernel/sys_types.h>

#define PROCESS_CMDLINE_BUF_SIZE                      256

STATIC_ASSERT(IS_PAGE_ALIGNED(KERNEL_STACK_SIZE));
STATIC_ASSERT(IS_PAGE_ALIGNED(IO_COPYBUF_SIZE));
STATIC_ASSERT(IS_PAGE_ALIGNED(ARGS_COPYBUF_SIZE));

struct kernel_alloc {

   struct bintree_node node;
   void *vaddr;
   size_t size;
};

struct mappings_info {

   struct kmalloc_heap *mmap_heap;
   struct list mappings;
};

struct process {

   REF_COUNTED_OBJECT;

   int pid;                          /* process id (tgid in the Linux kernel) */
   int pgid;                         /* process group ID (same as in Linux)   */
   int sid;                          /* process session ID (as in Linux)      */
   int parent_pid;
   pdir_t *pdir;

   void *brk;
   void *initial_brk;
   struct mappings_info *mi;

   struct list children;

   void *proc_tty;
   bool did_call_execve;
   bool did_set_tty_medium_raw;

   /* This process is a result of vfork(), before any call to execve() */
   bool vforked;
   bool inherited_mmap_heap;

   int *set_child_tid;                    /* NOTE: this is an user pointer */

   struct kmutex fslock;                  /* protects `handles` and `cwd` */
   mode_t umask;

   struct vfs_path cwd;                   /* CWD as a struct vfs_path */
   char *debug_cmdline;                   /* debug field used by debugpanel */

   struct locked_file *elf;

   /* large members */
   char str_cwd[MAX_PATH];                /* current working directory */
   fs_handle handles[MAX_HANDLES];        /* just a small fixed-size array */

   __sighandler_t sa_handlers[_NSIG];
   ulong sa_mask[K_SIGACTION_MASK_WORDS];
   ulong sa_flags;
};

struct misc_buf {

   char path_buf[MAX_PATH];
   char unused[1024 - MAX_PATH];
   char execve_ctx[1024];
   char resolve_ctx[2048];
};

STATIC_ASSERT(sizeof(struct misc_buf) <= PAGE_SIZE);

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
   ATOMIC(int) term_sig;

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

   /*
    * For kernel threads, this is a function pointer of the thread's entry
    * point. For user processes/threads, it is unused for the moment. In the
    * future, for processes it could be a path to the executable and for threads
    * still the entry-point.
    */
   void *what;

   /*
    * The purpose of having this opaque `arch_fields` member here is to avoid
    * including hal.h in this header. The general idea is that, while it is OK
    * to have architecture-specific fields in this struct, all the non-arch code
    * should be able to work with this structure without needing to understand
    * the arch-specific fields. It's all about trying to isolate as much as
    * possible non-arch code from arch-specific code.
    *
    * NOTE: a simpler solution here might be just using a void * pointer and
    * having those fields stored elsewhere in the kernel heap. That will
    * certainly work, but it will require an additional kmalloc/kfree call for
    * each task creation/destruction _and_ it will increase the likelihood of
    * a cache miss when trying to access those fields as they won't be in a
    * memory location contiguous with the current struct.
    *
    * Therefore, this approach, at the price of some complexity, achieves
    * separation of arch from non-arch code without introducing any runtime
    * cost for that.
    */
   char arch_fields[ARCH_TASK_MEMBERS_SIZE] ALIGNED_AT(ARCH_TASK_MEMBERS_ALIGN);
};

#define TOT_PROC_AND_TASK_SIZE    (sizeof(struct task) + sizeof(struct process))

STATIC_ASSERT((sizeof(struct task) & ~POINTER_ALIGN_MASK) == 0);
STATIC_ASSERT((sizeof(struct process) & ~POINTER_ALIGN_MASK) == 0);

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

static ALWAYS_INLINE bool is_tasklet_runner(struct task *ti)
{
   return ti->what == &tasklet_runner;
}

static ALWAYS_INLINE bool
task_is_parent(struct task *parent, struct task *child)
{
   return child->pi->parent_pid == parent->pi->pid;
}

enum wakeup_reason {
   task_died,
   task_stopped,
   task_continued,
};

int do_fork(bool vfork);
void handle_vforked_child_move_on(struct process *pi);
int first_execve(const char *abs_path, const char *const *argv);

int setup_usermode_task(struct elf_program_info *pinfo,
                        struct task *task_to_use,
                        const char *const *argv,
                        const char *const *env,
                        struct task **ti_ref,
                        regs_t *user_regs);

void finalize_usermode_task_setup(struct task *ti, regs_t *user_regs);
void set_current_task_in_kernel(void);
void set_current_task_in_user_mode(void);

struct task *
allocate_new_process(struct task *parent, int pid, pdir_t *new_pdir);

struct task *
allocate_new_thread(struct process *pi, int tid, bool alloc_bufs);

void free_task(struct task *ti);
void free_mem_for_zombie_task(struct task *ti);
bool arch_specific_new_task_setup(struct task *ti, struct task *parent);
void arch_specific_free_task(struct task *ti);
void wake_up_tasks_waiting_on(struct task *ti, enum wakeup_reason r);
void init_process_lists(struct process *pi);

void *task_temp_kernel_alloc(size_t size);
void task_temp_kernel_free(void *ptr);

void process_set_cwd2_nolock(struct vfs_path *tp);
void process_set_cwd2_nolock_raw(struct process *pi, struct vfs_path *tp);
void terminate_process(int exit_code, int term_sig);
void close_cloexec_handles(struct process *pi);
