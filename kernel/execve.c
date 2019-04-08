/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/elf_loader.h>

static char *const default_env[] =
{
   "OSTYPE=linux-gnu",
   "TERM=linux",
   "CONSOLE=/dev/console",
   "TILCK=1",
   NULL,
};

static int
execve_get_path(const char *user_filename, char **abs_path_ref)
{
   int rc = 0;
   task_info *curr = get_curr_task();
   char *abs_path = curr->io_copybuf;
   char *orig_file_path = curr->args_copybuf;
   size_t written = 0;

   if (UNLIKELY(curr == kernel_process)) {
      *abs_path_ref = (char *)user_filename;
      goto out;
   }

   rc = duplicate_user_path(orig_file_path,
                            user_filename,
                            MIN((uptr)MAX_PATH, ARGS_COPYBUF_SIZE),
                            &written);

   if (rc != 0)
      goto out;

   STATIC_ASSERT(IO_COPYBUF_SIZE >= MAX_PATH);

   rc = compute_abs_path(orig_file_path, curr->pi->cwd, abs_path, MAX_PATH);

   if (rc != 0)
      goto out;

   *abs_path_ref = abs_path;

out:
   return rc;
}


static int
execve_get_args(const char *const *user_argv,
                const char *const *user_env,
                char *const **argv_ref,
                char *const **env_ref)
{
   int rc = 0;
   char *const *argv = NULL;
   char *const *env = NULL;
   task_info *curr = get_curr_task();

   char *dest = (char *)curr->args_copybuf;
   size_t written = 0;

   if (UNLIKELY(curr == kernel_process)) {
      *argv_ref = (char *const *)user_argv;
      *env_ref = (char *const *)user_env;
      goto out;
   }

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
   void *entry;
   void *stack_addr;
   void *brk;
   char *abs_path;
   char *const *argv = NULL;
   char *const *env = NULL;
   pdir_t *pdir = NULL;
   task_info *ti = NULL;
   process_info *pi;

   ASSERT(get_curr_task() != NULL);

   task_info *curr =
      get_curr_task() != kernel_process ? get_curr_task() : NULL;

   if ((rc = execve_get_path(user_filename, &abs_path)))
      goto errend2;

   if ((rc = execve_get_args(user_argv, user_env, &argv, &env)))
      goto errend2;

   char *const default_argv[] = { abs_path, NULL };

   /*
    * Wishfully, it would be great to NOT keep the preemption disabled during
    * the whole load_elf_program() as it does VFS calls. But, the problem is
    * that the function changes the current page dir in order to being able to
    * use new image's vaddrs. Preemption is NOT possible because task->pi->pdir
    * still points to the older page directory, until the whole load_elf_program
    * completes.
    */
   disable_preemption();

   if ((rc = load_elf_program(abs_path, &pdir, &entry, &stack_addr, &brk)))
      goto errend;

   rc = setup_usermode_task(pdir,
                            entry,
                            stack_addr,
                            curr,
                            argv ? argv : default_argv,
                            env ? env : default_env,
                            &ti);

   if (rc)
      goto errend;

   enable_preemption();


   ASSERT(ti != NULL);
   pi = ti->pi;
   pi->brk = brk;
   pi->initial_brk = brk;
   pi->did_call_execve = true;
   memcpy(pi->filepath, abs_path, strlen(abs_path) + 1);
   close_cloexec_handles(pi);

   disable_preemption();

   if (LIKELY(curr != NULL)) {
      pop_nested_interrupt();
      switch_to_task(ti, -1);
   }

   switch_to_idle_task();
   NOT_REACHED();

errend:
   enable_preemption();
errend2:
   ASSERT(rc != 0);
   return rc;
}
