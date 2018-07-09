
#include <exos/kernel/process.h>
#include <exos/kernel/errno.h>
#include <exos/kernel/user.h>
#include <exos/kernel/elf_loader.h>

static char *const default_env[] =
{
   "OSTYPE=linux-gnu",
   "TERM=linux",
   "CONSOLE=/dev/tty",
   "EXOS=1",
   NULL
};

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
