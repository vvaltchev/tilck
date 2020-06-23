/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/paging.h>
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
   fs_handle handles[MAX_HANDLES];        /* just a small fixed-size array */

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
   char arch_fields[ARCH_PROC_MEMBERS_SIZE] ALIGNED_AT(ARCH_PROC_MEMBERS_ALIGN);

   /* large members */
   char str_cwd[MAX_PATH];                /* current working directory */

   __sighandler_t sa_handlers[_NSIG];
   ulong sa_mask[K_SIGACTION_MASK_WORDS];
   ulong sa_flags;
};

STATIC_ASSERT(sizeof(struct misc_buf) <= PAGE_SIZE);

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

static ALWAYS_INLINE bool
task_is_parent(struct task *parent, struct task *child)
{
   return child->pi->parent_pid == parent->pi->pid;
}

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

struct task *
allocate_new_process(struct task *parent, int pid, pdir_t *new_pdir);

struct task *
allocate_new_thread(struct process *pi, int tid, bool alloc_bufs);

void free_task(struct task *ti);
void free_mem_for_zombie_task(struct task *ti);
bool arch_specific_new_task_setup(struct task *ti, struct task *parent);
void arch_specific_free_task(struct task *ti);
void arch_specific_new_proc_setup(struct process *pi, struct process *parent);
void arch_specific_free_proc(struct process *pi);
void wake_up_tasks_waiting_on(struct task *ti, enum wakeup_reason r);
void init_process_lists(struct process *pi);

void process_set_cwd2_nolock(struct vfs_path *tp);
void process_set_cwd2_nolock_raw(struct process *pi, struct vfs_path *tp);
void terminate_process(int exit_code, int term_sig);
void close_cloexec_handles(struct process *pi);
