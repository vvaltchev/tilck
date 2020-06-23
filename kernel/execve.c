/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/elf_loader.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/debug_utils.h>

static const char *const default_env[] =
{
   "OSTYPE=linux-gnu",
   "TERM=linux",
   "LANG=C",
   "LC_ALL=C",
   "CONSOLE=/dev/console",
   "TILCK=1",
   "HOME=/",
   "PS1=\\[\\e[01;32m\\]\\u@\\h\\[\\e[00m\\]:$PWD# ",
   NULL,
};

struct execve_ctx {

   struct task *curr_user_task;
   const char *const *env;
   int reclvl;

   char hdr_stack[MAX_SCRIPT_REC + 1][ELF_RAW_HEADER_SIZE];
   const char *argv_stack[MAX_SCRIPT_REC][USERAPP_MAX_ARGS_COUNT];
};

static int
do_execve_int(struct execve_ctx *ctx,
              const char *path,
              const char *const *argv);

static int
execve_get_path(const char *user_path, char **path_ref)
{
   int rc = 0;
   struct task *curr = get_curr_task();
   char *path = curr->misc_buf->path_buf;
   size_t written = 0;

   if (!(rc = duplicate_user_path(path, user_path, MAX_PATH, &written)))
      *path_ref = path;

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
   struct task *curr = get_curr_task();

   char *dest = (char *)curr->args_copybuf;
   size_t written = 0;

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

static void
save_cmdline(struct process *pi, const char *const *argv)
{

   char *p = pi->debug_cmdline;
   char *const end = p + PROCESS_CMDLINE_BUF_SIZE;

   for (int i = 0; argv[i]; i++) {

      for (const char *s = argv[i]; *s && p < end; s++)
         *p++ = *s;

      if (p == end)
         break;

      *p++ = ' ';
   }

   if (p < end)
      *p = 0;
   else
      memcpy(p - 4, "...", 4);
}

static void
execve_final_steps(struct task *ti,
                   void *brk,
                   const char *const *argv,
                   regs_t *user_regs)
{
   struct process *pi = ti->pi;
   ASSERT(!is_preemption_enabled());

   finalize_usermode_task_setup(ti, user_regs);

   /* Final steps */
   pi->brk = brk;
   pi->initial_brk = brk;
   pi->did_call_execve = true;

   if (pi->debug_cmdline)
      save_cmdline(pi, argv);

   if (pi->vforked)
      handle_vforked_child_move_on(pi);
}

static inline int
execve_handle_script(struct execve_ctx *ctx,
                     const char *path,
                     const char *const *argv)
{
   const char **new_argv = ctx->argv_stack[ctx->reclvl];
   char *hdr = ctx->hdr_stack[ctx->reclvl];
   const char **na = new_argv;
   int i;

   hdr[ELF_RAW_HEADER_SIZE - 1] = 0;

   for (i = 0; argv[i]; i++) {
      if (i >= USERAPP_MAX_ARGS_COUNT - 2)
         return -E2BIG; /* too many args */
   }

   hdr += 2; /* skip the shebang sequence ("#!") */

   /* skip the spaces between #! and the beginning of the path */
   while (*hdr == ' ') hdr++;

   /* if nothing is left, that's a bad executable */
   if (!*hdr)
      return -ENOEXEC;

   for (char *l = NULL, *p = hdr; *p && p < hdr + ELF_RAW_HEADER_SIZE; p++) {

      if (*p == ' ' && !l) {
         *p++ = 0;
         l = p;
         *na++ = hdr;
      }

      if (*p == '\n') {

         *p = 0;
         *na++ = l ? l : hdr;

         for (i = 0; argv[i]; i++)
            na[i] = argv[i];

         na[i] = NULL;
         ctx->reclvl++;
         new_argv[l ? 2 : 1] = path;
         return do_execve_int(ctx, new_argv[0], new_argv);
      }
   }

   /* We did not hit '\n': this cannot be accepted as a valid script */
   return -ENOEXEC;
}

static inline void
execve_do_task_switch(struct execve_ctx *ctx, struct task *ti)
{
   if (LIKELY(ctx->curr_user_task != NULL)) {

      ASSERT(ti == ctx->curr_user_task);

      /*
       * Handling the difference between the first execve() and all the
       * others. In case of a regular execve(), curr_user_task will always
       * be != NULL and we can switch again to its 'new image' with
       * switch_to_task().
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

static int
execve_load_elf(struct execve_ctx *ctx,
                const char *path,
                const char *const *argv,
                struct elf_program_info *pinfo)
{
   char *hdr = ctx->hdr_stack[ctx->reclvl];
   int rc;

   bzero(hdr, ELF_RAW_HEADER_SIZE);

   if ((rc = load_elf_program(path, hdr, pinfo))) {
      if (rc == -ENOEXEC && hdr[0] == '#' && hdr[1] == '!') {

         if (ctx->reclvl == MAX_SCRIPT_REC)
            return -ELOOP;

         rc = execve_handle_script(ctx, path, argv);
      }
      return rc;
   }

   return 0;
}

static int
do_execve_int(struct execve_ctx *ctx, const char *path, const char *const *argv)
{
   struct elf_program_info pinfo = {0};
   struct task *ti = NULL;
   regs_t user_regs;
   int rc;

   DEBUG_VALIDATE_STACK_PTR();
   ASSERT(is_preemption_enabled());

   if ((rc = execve_load_elf(ctx, path, argv, &pinfo)))
      return rc;                 /* load failed */

   disable_preemption();
   {
      rc = setup_process(&pinfo,
                         ctx->curr_user_task,
                         argv,
                         ctx->env,
                         &ti,
                         &user_regs);
   }
   enable_preemption();

   if (UNLIKELY(rc))
      return rc;                 /* setup_process() failed */

   /* From now on, we cannot fail */
   close_cloexec_handles(ti->pi);
   disable_preemption();
   {
      execve_final_steps(ti, pinfo.brk, argv, &user_regs);
      execve_do_task_switch(ctx, ti); /* this might NOT return */
   }
   enable_preemption();
   return rc;
}

static int
do_execve(struct task *curr_user_task,
          const char *path,
          const char *const *argv,
          const char *const *env)
{
   struct task *ti = get_curr_task();
   const char *const default_argv[] = { path, NULL };

   struct execve_ctx *ctx = (void *) ti->misc_buf->execve_ctx;
   STATIC_ASSERT(sizeof(*ctx) <= sizeof(ti->misc_buf->execve_ctx));

   ctx->curr_user_task = curr_user_task;
   ctx->env = env ? env : default_env;
   ctx->reclvl = 0;

   return do_execve_int(ctx, path, argv ? argv : default_argv);
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

   struct task *curr = get_curr_task();
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
