/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/elf_loader.h>
#include <tilck/kernel/syscalls.h>

static const char *const default_env[] =
{
   "OSTYPE=linux-gnu",
   "TERM=linux",
   "CONSOLE=/dev/console",
   "TILCK=1",
   NULL,
};

static int
execve_get_path(const char *user_path, char **path_ref)
{
   int rc = 0;
   task_info *curr = get_curr_task();
   char *path = curr->io_copybuf;
   size_t written = 0;
   STATIC_ASSERT(IO_COPYBUF_SIZE > MAX_PATH);

   if (LIKELY(curr != kernel_process)) {

      if (!(rc = duplicate_user_path(path, user_path, MAX_PATH, &written)))
         *path_ref = path;

   } else {

     *path_ref = (char *)user_path;
   }

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

static inline void
execve_prepare_process(process_info *pi, void *brk, const char *path)
{
   /*
    * Close the CLOEXEC handles. Note: we couldn't do that before because they
    * can be closed ONLY IF execve() succeeded. Only here we're sure of that.
    */

   close_cloexec_handles(pi);

   /* Final steps */
   pi->brk = brk;
   pi->initial_brk = brk;
   pi->did_call_execve = true;

   /*
    * TODO: here we might need the canonical file path instead of just any
    * usable path. In alternative, we might, even better use a vfs_path instead
    * of a string path.
    */
   memcpy(pi->filepath, path, strlen(path) + 1);
}

static int
do_execve(task_info *curr_user_task,
          const char *path,
          const char *const *argv,
          const char *const *env)
{
   int rc;
   pdir_t *pdir = NULL;
   task_info *ti = NULL;
   void *entry, *stack_addr, *brk;
   const char *const default_argv[] = { path, NULL };

   ASSERT(is_preemption_enabled());

   if ((rc = load_elf_program(path, &pdir, &entry, &stack_addr, &brk)))
      return rc;

   disable_preemption();

   rc = setup_usermode_task(pdir,
                            entry,
                            stack_addr,
                            curr_user_task,
                            argv ? argv : default_argv,
                            env ? env : default_env,
                            &ti);

   if (LIKELY(!rc)) {

      /* Positive case: setup_usermode_task() succeeded */
      execve_prepare_process(ti->pi, brk, path);

      if (LIKELY(curr_user_task != NULL)) {

         ASSERT(ti == curr_user_task);

         /*
          * This is 2nd `if` handling the difference between the first execve()
          * and all the others. In case of a regular execve(), curr_user_task
          * will always be != NULL and we can switch again to its 'new image'
          * with switch_to_task().
          */
         switch_to_task(ti, SYSCALL_SOFT_INTERRUPT);

      } else {

         /*
          * In case of curr_user_task == NULL (meaning first_execve()), we just
          * have to return. Yes, that's weird for execve(), but it makes perfect
          * sense in our context because we'll calling it from a pretty regular
          * kernel thread (do_async_init): we CANNOT just switch to another task
          * without saving the current state etc. We just have to return 0.
          */
      }
   }

   enable_preemption();
   return rc;
}

int first_execve(const char *path, const char *const *argv)
{
   return do_execve(NULL, path, argv, NULL);
}

int sys_execve(const char *user_filename,
               const char *const *user_argv,
               const char *const *user_env)
{
   int rc;
   char *path;
   char *const *argv = NULL;
   char *const *env = NULL;

   task_info *curr = get_curr_task();
   ASSERT(curr != NULL);

   if ((rc = execve_get_path(user_filename, &path)))
      return rc;

   if ((rc = execve_get_args(user_argv, user_env, &argv, &env)))
      return rc;

   return do_execve(curr,
                    path,
                    (const char *const *)argv,
                    (const char *const *)env);
}
