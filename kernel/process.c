
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/list.h>
#include <exos/kmalloc.h>
#include <exos/process.h>
#include <exos/hal.h>
#include <exos/elf_loader.h>
#include <exos/errno.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)

#define EXITCODE(ret, sig)  ((ret) << 8 | (sig))
#define STOPCODE(sig) ((sig) << 8 | 0x7f)
#define CONTINUED 0xffff
#define COREFLAG 0x80

task_info *allocate_new_process(task_info *parent)
{
   task_info *ti = kmalloc(sizeof(task_info));
   process_info *pi;

   if (!ti)
      return NULL;

   pi = kzmalloc(sizeof(process_info));

   if (!pi) {
      kfree2(ti, sizeof(task_info));
      return NULL;
   }

   pi->ref_count = 1;

   if (parent) {
      memcpy(ti, parent, sizeof(task_info));
      memcpy(pi, parent->pi, sizeof(process_info));
   } else {
      bzero(ti, sizeof(task_info));
   }

   ti->pi = pi;
   bintree_node_init(&ti->tree_by_tid);
   list_node_init(&ti->runnable_list);
   list_node_init(&ti->sleeping_list);

   return ti;
}

void free_task(task_info *ti)
{
   ASSERT(ti->state == TASK_STATE_ZOMBIE);

   kfree2(ti->kernel_stack, KTHREAD_STACK_SIZE);

   if (ti->tid == ti->owning_process_pid) {

      if (--ti->pi->ref_count == 0)
         kfree2(ti->pi, sizeof(process_info));
   }

   kfree2(ti, sizeof(task_info));
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

sptr sys_chdir(const char *path)
{
   sptr rc = 0;

   disable_preemption();
   {
      size_t path_len = strlen(path) + 1;

      if (path_len > ARRAY_SIZE(current->pi->cwd)) {
         rc = -ENAMETOOLONG;
         goto out;
      }

      memcpy(current->pi->cwd, path, path_len);
   }

out:
   enable_preemption();
   return rc;
}

sptr sys_getcwd(char *buf, size_t buf_size)
{
   size_t cwd_len;
   disable_preemption();
   {
      cwd_len = strlen(current->pi->cwd) + 1;

      if (!buf)
         return -EINVAL;

      if (buf_size < cwd_len)
         return -ERANGE;

      memcpy(buf, current->pi->cwd, cwd_len);
   }
   enable_preemption();
   return cwd_len;
}

sptr sys_execve(const char *filename,
                const char *const *argv,
                const char *const *env)
{
   int rc = -ENOENT; /* default, kind-of random, error */
   page_directory_t *pdir = NULL;
   char *filename_copy = NULL;
   char *const *argv_copy = NULL;
   char *const *env_copy = NULL;
   void *entry_point;
   void *stack_addr;

   filename_copy = strdup(filename);

   if (filename && !filename_copy) {
      rc = -ENOMEM;
      goto errend2;
   }

   argv_copy = dcopy_strarray(argv);

   if (argv && !argv_copy) {
      rc = -ENOMEM;
      goto errend2;
   }

   env_copy = dcopy_strarray(env);

   if (env && !env_copy) {
      rc = -ENOMEM;
      goto errend2;
   }

   disable_preemption();

   rc = load_elf_program(filename, &pdir, &entry_point, &stack_addr);

   if (rc < 0) {
      goto errend;
   }

   char *const default_argv[] = { filename_copy, NULL };

   if (LIKELY(current != NULL)) {
      task_change_state(current, TASK_STATE_RUNNABLE);
      pdir_destroy(current->pi->pdir);
   }

   create_usermode_task(pdir,
                        entry_point,
                        stack_addr,
                        current,
                        argv_copy ? argv_copy : default_argv,
                        env_copy ? env_copy : default_env);

   /* Free the duplicated buffers */
   kfree(filename_copy);
   dfree_strarray(argv_copy);
   dfree_strarray(env_copy);

   if (UNLIKELY(!current)) {

      /* Just counter-balance the disable_preemption() above */
      enable_preemption();

      /* We're still in the initialization and preemption is disabled */
      ASSERT(!is_preemption_enabled());

      /* Compensate the pop_nested_interrupt() in switch_to_task() */
      push_nested_interrupt(-1);
   }

   switch_to_idle_task();
   /* this point is unreachable */

errend:
   enable_preemption();

errend2:
   VERIFY(rc != 0);

   /* Free the duplicated buffers */
   kfree(filename_copy);
   dfree_strarray(argv_copy);
   dfree_strarray(env_copy);
   return rc;
}

sptr sys_pause()
{
   task_change_state(current, TASK_STATE_SLEEPING);
   kernel_yield();
   return 0;
}

sptr sys_getpid()
{
   ASSERT(current != NULL);
   return current->owning_process_pid;
}

sptr sys_waitpid(int pid, int *wstatus, int options)
{
   ASSERT(are_interrupts_enabled());

   // printk("[kernel] Pid %i will WAIT until pid %i dies\n",
   //        current->pid, pid);

   ASSERT(are_interrupts_enabled());
   DEBUG_VALIDATE_STACK_PTR();

   if (pid > 0) {

      /* Wait for a specific PID */

      volatile task_info *waited_task = (volatile task_info *)get_task(pid);

      if (!waited_task) {
         return -ECHILD;
      }

      while (waited_task->state != TASK_STATE_ZOMBIE) {
         wait_obj_set(&current->wobj, WOBJ_PID, (task_info *)waited_task);
         task_change_state(current, TASK_STATE_SLEEPING);
         kernel_yield();
      }

      if (wstatus) {
         *wstatus = EXITCODE(waited_task->exit_status, 0);
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

NORETURN void sys_exit(int exit_status)
{
   disable_preemption();

   // printk("[kernel] Exit process %i with code = %i\n",
   //        current->pid,
   //        exit_status);

   task_change_state(current, TASK_STATE_ZOMBIE);
   current->exit_status = exit_status;

   // Close all of its opened handles

   for (size_t i = 0; i < ARRAY_SIZE(current->pi->handles); i++) {

      fs_handle *h = current->pi->handles[i];

      if (h) {
         exvfs_close(h);
         current->pi->handles[i] = NULL;
      }
   }


   // Wake-up all the tasks waiting on this task to exit

   task_info *pos;

   list_for_each(pos, &sleeping_tasks_list, sleeping_list) {

      ASSERT(pos->state == TASK_STATE_SLEEPING);

      if (pos->wobj.ptr == current) {
         ASSERT(pos->wobj.type == WOBJ_PID);
         wait_obj_reset(&pos->wobj);
         task_change_state(pos, TASK_STATE_RUNNABLE);
      }
   }

   // We CANNOT free current->kernel_task here because we're using it!

   set_page_directory(get_kernel_page_dir());
   pdir_destroy(current->pi->pdir);

#ifdef DEBUG_QEMU_EXIT_ON_INIT_EXIT
   if (current->tid == 1) {
      debug_qemu_turn_off_machine();
   }
#endif

   schedule();
   NOT_REACHED();
}

// Returns child's pid
sptr sys_fork(void)
{
   disable_preemption();

   task_info *child = allocate_new_process(current);
   VERIFY(child != NULL); // TODO: handle this

   if (child->state == TASK_STATE_RUNNING) {
      child->state = TASK_STATE_RUNNABLE;
   }

   child->pi->pdir = pdir_clone(current->pi->pdir);

   /*
    * When a new process is created, its tid == its pid. After that, when that
    * process will create threads, the owning_process_pid will remain the same
    * but the tid will change.
    */
   child->owning_process_pid = create_new_pid();
   child->tid = child->owning_process_pid;
   child->parent_tid = current->tid;

   child->running_in_kernel = false;
   child->kernel_stack = kzmalloc(KTHREAD_STACK_SIZE);
   VERIFY(child->kernel_stack != NULL); // TODO: handle this OOM condition
   task_info_reset_kernel_stack(child);
   set_return_register(&child->state_regs, 0);
   add_task(child);

   // Make the parent to get child's pid as return value.
   set_return_register(&current->state_regs, child->tid);

   /* Duplicate all the handles */
   for (size_t i = 0; i < ARRAY_SIZE(child->pi->handles); i++) {
      if (child->pi->handles[i])
         child->pi->handles[i] = exvfs_dup(child->pi->handles[i]);
   }

   /*
    * Force the CR3 reflush using the current (parent's) pdir.
    * Without doing that, COW on parent's pages doesn't work immediately.
    * That is better (in this case) than invalidating all the pages affected,
    * one by one.
    */

   set_page_directory(current->pi->pdir);

   enable_preemption();
   return child->tid;
}
