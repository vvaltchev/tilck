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

static inline void
execve_prepare_process(process_info *pi, void *brk, const char *abs_path)
{
   pi->brk = brk;
   pi->initial_brk = brk;
   pi->did_call_execve = true;
   memcpy(pi->filepath, abs_path, strlen(abs_path) + 1);
   close_cloexec_handles(pi);
}

int first_execve(const char *abs_path, const char *const *argv)
{
   int rc;
   void *entry, *stack_addr, *brk;
   pdir_t *pdir = NULL;
   task_info *ti = NULL;

   if ((rc = load_elf_program(abs_path, &pdir, &entry, &stack_addr, &brk)))
      return rc;

   rc = setup_usermode_task(pdir,
                            entry,
                            stack_addr,
                            NULL,
                            (char *const *)argv,
                            default_env,
                            &ti);

   if (rc)
      return rc;

   ASSERT(ti != NULL);
   execve_prepare_process(ti->pi, brk, abs_path);

   push_nested_interrupt(-1);
   switch_to_idle_task();
   NOT_REACHED();
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

   task_info *curr = get_curr_task();
   ASSERT(curr != NULL);

   if ((rc = execve_get_path(user_filename, &abs_path)))
      return rc;

   if ((rc = execve_get_args(user_argv, user_env, &argv, &env)))
      return rc;

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

   execve_prepare_process(ti->pi, brk, abs_path);
   pop_nested_interrupt();
   switch_to_task(ti, -1);

errend:
   enable_preemption();
   ASSERT(rc != 0);
   return rc;
}
