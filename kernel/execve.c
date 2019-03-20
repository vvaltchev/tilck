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
   NULL
};


static int
execve_get_path(const char *user_filename, char **abs_path_ref)
{
   int rc = 0;
   char *abs_path = NULL;
   char *orig_file_path;
   task_info *curr = get_curr_task();

   char *dest = (char *)curr->args_copybuf;
   size_t written = 0;

   if (UNLIKELY(curr == kernel_process)) {
      *abs_path_ref = (char *)user_filename;
      goto out;
   }

   orig_file_path = dest;
   rc = duplicate_user_path(dest,
                            user_filename,
                            MIN((uptr)MAX_PATH, ARGS_COPYBUF_SIZE),
                            &written);

   if (rc != 0)
      goto out;

   STATIC_ASSERT(IO_COPYBUF_SIZE >= MAX_PATH);

   abs_path = curr->io_copybuf;
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
   void *entry_point;
   void *stack_addr;
   void *brk;
   char *abs_path;
   char *const *argv = NULL;
   char *const *env = NULL;
   page_directory_t *pdir = NULL;
   task_info *ti = NULL;

   task_info *curr = get_curr_task();
   ASSERT(curr != NULL);

   disable_preemption();

   rc = execve_get_path(user_filename, &abs_path);

   if (rc != 0)
      goto errend;

   rc = execve_get_args(user_argv, user_env, &argv, &env);

   if (rc != 0)
      goto errend;

   rc = load_elf_program(abs_path, &pdir, &entry_point, &stack_addr, &brk);

   if (rc != 0)
      goto errend;

   char *const default_argv[] = { abs_path, NULL };

   if (LIKELY(curr != kernel_process)) {
      task_change_state(curr, TASK_STATE_RUNNABLE);
      pdir_destroy(curr->pi->pdir);
   }

   rc = create_usermode_task(pdir,
                             entry_point,
                             stack_addr,
                             curr != kernel_process ? curr : NULL,
                             argv ? argv : default_argv,
                             env ? env : default_env,
                             &ti);

   if (rc)
      goto errend;

   ASSERT(ti != NULL);

   ti->pi->brk = brk;
   ti->pi->initial_brk = brk;
   memcpy(ti->pi->filepath, abs_path, strlen(abs_path) + 1);

   switch_to_idle_task();
   NOT_REACHED();

errend:
   enable_preemption();
   ASSERT(rc != 0);
   return rc;
}
