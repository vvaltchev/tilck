
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/list.h>
#include <exos/kmalloc.h>
#include <exos/process.h>
#include <exos/hal.h>
#include <exos/elf_loader.h>
#include <exos/errno.h>
#include <exos/user.h>
#include <exos/debug_utils.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)

#define EXITCODE(ret, sig)  ((ret) << 8 | (sig))
#define STOPCODE(sig) ((sig) << 8 | 0x7f)
#define CONTINUED 0xffff
#define COREFLAG 0x80

static bool do_common_task_allocations(task_info *ti)
{
   ti->kernel_stack = kzmalloc(KTHREAD_STACK_SIZE);

   if (!ti->kernel_stack)
      return false;

   ti->io_copybuf = kmalloc(IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE);

   if (!ti->io_copybuf) {
      kfree2(ti->kernel_stack, KTHREAD_STACK_SIZE);
      return false;
   }

   ti->args_copybuf = (void *)((uptr)ti->io_copybuf + IO_COPYBUF_SIZE);
   return true;
}

static void internal_free_mem_for_zombie_task(task_info *ti)
{
   kfree2(ti->io_copybuf, IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE);
   kfree2(ti->kernel_stack, KTHREAD_STACK_SIZE);

   ti->io_copybuf = NULL;
   ti->args_copybuf = NULL;
   ti->kernel_stack = NULL;
}

void free_mem_for_zombie_task(task_info *ti)
{
   ASSERT(ti->state == TASK_STATE_ZOMBIE);

#ifdef DEBUG

   uptr stack_var = 123;
   if (((uptr)&stack_var & PAGE_MASK) != (uptr)&kernel_initial_stack)
      panic("free_mem_for_zombie_task() called w/o switch to initial stack");

#endif

   internal_free_mem_for_zombie_task(ti);
}

task_info *allocate_new_process(task_info *parent, int pid)
{
   process_info *pi;
   task_info *ti = kmalloc(sizeof(task_info) + sizeof(process_info));

   if (!ti)
      return NULL;

   pi = (process_info *)(ti + 1);

   if (parent) {

      memcpy(ti, parent, sizeof(task_info));
      memcpy(pi, parent->pi, sizeof(process_info));
      pi->parent_pid = parent->tid;
      pi->mmap_heap = kmalloc_heap_dup(parent->pi->mmap_heap);

   } else {

      bzero(ti, sizeof(task_info) + sizeof(process_info));
      /* NOTE: parent_pid in this case is 0 as kernel_process->pi->tid */
   }

   if (!do_common_task_allocations(ti)) {
      kfree2(ti, sizeof(task_info) + sizeof(process_info));
      return NULL;
   }

   pi->ref_count = 1;
   ti->tid = pid; /* here tid is a PID */
   ti->owning_process_pid = pid;

   ti->pi = pi;
   bintree_node_init(&ti->tree_by_tid);
   list_node_init(&ti->runnable_list);
   list_node_init(&ti->sleeping_list);

   arch_specific_new_task_setup(ti);
   return ti;
}

task_info *allocate_new_thread(process_info *pi)
{
   task_info *proc = get_process_task(pi);
   task_info *ti = kzmalloc(sizeof(task_info));

   if (!ti || !do_common_task_allocations(ti)) {
      kfree2(ti, sizeof(task_info));
      return NULL;
   }

   ti->pi = pi;
   ti->tid = MAX_PID + (sptr)ti - KERNEL_BASE_VA;
   ti->owning_process_pid = proc->tid;

   arch_specific_new_task_setup(ti);
   return ti;
}

void free_task(task_info *ti)
{
   ASSERT(ti->state == TASK_STATE_ZOMBIE);
   arch_specific_free_task(ti);

   ASSERT(!ti->kernel_stack);
   ASSERT(!ti->io_copybuf);
   ASSERT(!ti->args_copybuf);

   if (ti->tid == ti->owning_process_pid) {

      ASSERT(ti->pi->ref_count > 0);

      if (ti->pi->mmap_heap)
         kmalloc_destroy_heap(ti->pi->mmap_heap);

      if (--ti->pi->ref_count == 0)
         kfree2(ti, sizeof(task_info) + sizeof(process_info));

   } else {

      kfree2(ti, sizeof(task_info));
   }
}


/*
 * ***************************************************************
 *
 * SYSCALLS
 *
 * ***************************************************************
 */

static char *const default_env[] =
{
   "OSTYPE=linux-gnu", "EXOS=1", NULL
};

sptr sys_chdir(const char *user_path)
{
   sptr rc = 0;
   task_info *curr = get_curr_task();
   process_info *pi = curr->pi;
   char *orig_path = curr->args_copybuf;
   char *path = curr->args_copybuf + ARGS_COPYBUF_SIZE / 2;

   STATIC_ASSERT(ARRAY_SIZE(pi->cwd) == MAX_PATH);
   STATIC_ASSERT((ARGS_COPYBUF_SIZE / 2) >= MAX_PATH);

   rc = copy_str_from_user(orig_path, user_path, MAX_PATH, NULL);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   disable_preemption();
   {
      rc = compute_abs_path(orig_path, pi->cwd, path, MAX_PATH);

      if (rc < 0) {
         rc = -ENAMETOOLONG;
         goto out;
      }

      fs_handle h = NULL;
      rc = exvfs_open(path, &h);

      if (rc < 0)
         goto out; /* keep the same rc */

      ASSERT(h != NULL);
      exvfs_close(h);

      u32 pl = strlen(path);
      memcpy(pi->cwd, path, pl + 1);

      if (pl > 1) {

         /* compute_abs_path always returns a path without a trailing '/' */
         ASSERT(pi->cwd[pl - 1] != '/');

         /* on the other side, pi->cwd has always a trailing '/' */
         pi->cwd[pl] = '/';
         pi->cwd[pl + 1] = 0;
      }
   }

out:
   enable_preemption();
   return rc;
}

sptr sys_getcwd(char *user_buf, size_t buf_size)
{
   sptr ret;
   size_t cwd_len;
   disable_preemption();
   {
      cwd_len = strlen(get_curr_task()->pi->cwd) + 1;

      if (!user_buf || !buf_size) {
         ret = -EINVAL;
         goto out;
      }

      if (buf_size < cwd_len) {
         ret = -ERANGE;
         goto out;
      }

      ret = copy_to_user(user_buf, get_curr_task()->pi->cwd, cwd_len);

      if (ret < 0) {
         ret = -EFAULT;
         goto out;
      }

      if (cwd_len > 2) { /* NOTE: cwd_len includes the trailing \0 */
         ASSERT(user_buf[cwd_len - 2] == '/');
         user_buf[cwd_len - 2] = 0; /* drop the trailing '/' */
      }

      ret = cwd_len;
   }

out:
   enable_preemption();
   return ret;
}

static int
execve_get_path_and_args(const char *user_filename,
                         const char *const *user_argv,
                         const char *const *user_env,
                         char **abs_path_ref,
                         char *const **argv_ref,
                         char *const **env_ref)
{
   int rc = 0;
   char *abs_path = NULL;
   char *const *argv = NULL;
   char *const *env = NULL;
   char *orig_file_path;
   task_info *curr = get_curr_task();

   char *dest = (char *)curr->args_copybuf;
   size_t written = 0;

   if (UNLIKELY(curr == kernel_process)) {
      *abs_path_ref = (char *)user_filename;
      *argv_ref = (char *const *)user_argv;
      *env_ref = (char *const *)user_env;
      goto out;
   }

   orig_file_path = dest;
   rc = duplicate_user_path(dest,
                            user_filename,
                            MIN(MAX_PATH, ARGS_COPYBUF_SIZE),
                            &written);

   if (rc != 0)
      goto out;

   STATIC_ASSERT(IO_COPYBUF_SIZE >= MAX_PATH);

   abs_path = curr->io_copybuf;
   rc = compute_abs_path(orig_file_path, curr->pi->cwd, abs_path, MAX_PATH);

   if (rc != 0)
      goto out;

   written += strlen(orig_file_path) + 1;

   if (user_argv) {
      argv = (char *const *) (dest + written);
      rc = duplicate_user_argv(dest,
                               user_argv,
                               ARGS_COPYBUF_SIZE,
                               &written);
      if (rc != 0)
         goto out;
   }

   if (user_env) {
      env = (char *const *) (dest + written);
      rc = duplicate_user_argv(dest,
                               user_env,
                               ARGS_COPYBUF_SIZE,
                               &written);
      if (rc != 0)
         goto out;
   }

   *abs_path_ref = abs_path;
   *argv_ref = argv;
   *env_ref = env;

out:
   return rc;
}

sptr sys_execve(const char *user_filename,
                const char *const *user_argv,
                const char *const *user_env)
{
   int rc;
   void *entry_point;
   void *stack_addr;
   void *brk;
   char *abs_path;
   char *const *argv = NULL;
   char *const *env = NULL;
   page_directory_t *pdir = NULL;

   task_info *curr = get_curr_task();
   ASSERT(curr != NULL);

   disable_preemption();

   rc = execve_get_path_and_args(user_filename,
                                 user_argv,
                                 user_env,
                                 &abs_path,
                                 &argv,
                                 &env);

   if (rc != 0)
      goto errend;

   rc = load_elf_program(abs_path, &pdir, &entry_point, &stack_addr, &brk);

   if (rc < 0)
      goto errend;

   char *const default_argv[] = { abs_path, NULL };

   if (LIKELY(curr != kernel_process)) {
      task_change_state(curr, TASK_STATE_RUNNABLE);
      pdir_destroy(curr->pi->pdir);
   }

   task_info *ti =
      create_usermode_task(pdir,
                           entry_point,
                           stack_addr,
                           curr != kernel_process ? curr : NULL,
                           argv ? argv : default_argv,
                           env ? env : default_env);

   if (!ti) {
      rc = -ENOMEM;
      goto errend;
   }

   ti->pi->brk = brk;
   ti->pi->initial_brk = brk;

   switch_to_idle_task();
   NOT_REACHED();

errend:
   enable_preemption();
   VERIFY(rc != 0);
   return rc;
}

sptr sys_pause()
{
   task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
   kernel_yield();
   return 0;
}

sptr sys_getpid()
{
   return get_curr_task()->owning_process_pid;
}

sptr sys_gettid()
{
   return get_curr_task()->tid;
}

void join_kernel_thread(int tid)
{
   ASSERT(is_preemption_enabled());

   task_info *ti = get_task(tid);

   if (!ti)
      return; /* the thread already exited */

   while (get_task(tid) != NULL) {
      wait_obj_set(&get_curr_task()->wobj, WOBJ_TASK, ti);
      task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
      kernel_yield();
   }
}

sptr sys_waitpid(int pid, int *wstatus, int options)
{
   ASSERT(are_interrupts_enabled());
   DEBUG_VALIDATE_STACK_PTR();

   if (pid > 0) {

      /* Wait for a specific PID */

      volatile task_info *waited_task = (volatile task_info *)get_task(pid);

      if (!waited_task) {
         return -ECHILD;
      }

      while (waited_task->state != TASK_STATE_ZOMBIE) {

         wait_obj_set(&get_curr_task()->wobj,
                      WOBJ_TASK,
                      (task_info *)waited_task);
         task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
         kernel_yield();
      }

      if (wstatus) {
         int value = EXITCODE(waited_task->exit_status, 0);

         if (copy_to_user(wstatus, &value, sizeof(int)) < 0) {
            remove_task((task_info *)waited_task);
            return -EFAULT;
         }
      }

      remove_task((task_info *)waited_task);
      return pid;

   } else {

      /*
       * Since exOS does not support UIDs and GIDs != 0, the values of
       *    pid < -1
       *    pid == -1
       *    pid == 0
       * are treated in the same way.
       */

      NOT_IMPLEMENTED();
   }
}

sptr sys_wait4(int pid, int *wstatus, int options, void *user_rusage)

{
   char zero_buf[136] = {0};

   if (user_rusage) {
      // TODO: update when rusage is actually supported
      if (copy_to_user(user_rusage, zero_buf, sizeof(zero_buf)) < 0)
         return -EFAULT;
   }

   return sys_waitpid(pid, wstatus, options);
}

NORETURN sptr sys_exit(int exit_status)
{
   disable_preemption();
   task_info *curr = get_curr_task();

   // printk("Exit process %i with code = %i\n",
   //        current->pid,
   //        exit_status);

   task_change_state(curr, TASK_STATE_ZOMBIE);
   curr->exit_status = exit_status;

   // Close all of its opened handles

   for (size_t i = 0; i < ARRAY_SIZE(curr->pi->handles); i++) {

      fs_handle *h = curr->pi->handles[i];

      if (h) {
         exvfs_close(h);
         curr->pi->handles[i] = NULL;
      }
   }


   // Wake-up all the tasks waiting on this task to exit

   task_info *pos;

   list_for_each(pos, &sleeping_tasks_list, sleeping_list) {

      ASSERT(pos->state == TASK_STATE_SLEEPING);

      if (pos->wobj.ptr == curr) {
         ASSERT(pos->wobj.type == WOBJ_TASK);
         wait_obj_reset(&pos->wobj);
         task_change_state(pos, TASK_STATE_RUNNABLE);
      }
   }

   set_page_directory(get_kernel_page_dir());
   pdir_destroy(curr->pi->pdir);

#ifdef DEBUG_QEMU_EXIT_ON_INIT_EXIT
   if (curr->tid == 1) {
      debug_qemu_turn_off_machine();
   }
#endif

   /* WARNING: the following call discards the whole stack! */
   switch_to_initial_kernel_stack();

   /* Free the heap allocations used by the task, including the kernel stack */
   free_mem_for_zombie_task(curr);
   schedule();

   /* Necessary to guarantee to the compiler that we won't return. */
   NOT_REACHED();
}

NORETURN sptr sys_exit_group(int status)
{
   // TODO: update when user threads are supported
   sys_exit(status);
}

// Returns child's pid
sptr sys_fork(void)
{
   disable_preemption();
   task_info *curr = get_curr_task();

   int pid = create_new_pid();

   if (pid == -1) {
      enable_preemption();
      return -EAGAIN;
   }

   task_info *child = allocate_new_process(curr, pid);

   if (!child) {
      enable_preemption();
      return -ENOMEM;
   }

   ASSERT(child->kernel_stack != NULL);

   if (child->state == TASK_STATE_RUNNING) {
      child->state = TASK_STATE_RUNNABLE;
   }

   child->pi->pdir = pdir_clone(curr->pi->pdir);

   if (!child->pi->pdir)
      goto no_mem_exit;

   child->running_in_kernel = false;
   task_info_reset_kernel_stack(child);

   child->state_regs--; // make room for a regs struct in child's stack
   *child->state_regs = *curr->state_regs; // copy parent's regs
   set_return_register(child->state_regs, 0);

   // Make the parent to get child's pid as return value.
   set_return_register(curr->state_regs, child->tid);

   /* Duplicate all the handles */
   for (u32 i = 0; i < ARRAY_SIZE(child->pi->handles); i++) {

      fs_handle h = child->pi->handles[i];

      if (!h)
         continue;

      fs_handle dup_h = NULL;
      int rc = exvfs_dup(h, &dup_h);

      if (rc < 0 || !dup_h) {

         for (u32 j = 0; j < i; j++)
            exvfs_close(child->pi->handles[j]);

         goto no_mem_exit;
      }

      child->pi->handles[i] = dup_h;
   }

   add_task(child);

   /*
    * Force the CR3 reflush using the current (parent's) pdir.
    * Without doing that, COW on parent's pages doesn't work immediately.
    * That is better (in this case) than invalidating all the pages affected,
    * one by one.
    */

   set_page_directory(curr->pi->pdir);

   enable_preemption();
   return child->tid;

no_mem_exit:
   child->state = TASK_STATE_ZOMBIE;
   internal_free_mem_for_zombie_task(child);
   free_task(child);
   enable_preemption();
   return -ENOMEM;
}


#include <sys/prctl.h>

sptr sys_prctl(int option, uptr a2, uptr a3, uptr a4, uptr a5)
{
   if (option == PR_SET_NAME) {
      printk("[TID: %d] PR_SET_NAME '%s'\n", get_curr_task()->tid, a2);
      // TODO: save the task name in task_info.
      return 0;
   }

   printk("[TID: %d] Unknown option: %d\n", option);
   return -EINVAL;
}
