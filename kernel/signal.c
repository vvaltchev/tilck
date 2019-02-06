/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>

typedef void (*action_type)(task_info *, int signum);

static void action_terminate(task_info *ti, int signum)
{
   terminate_process(ti, 0, signum);
}

static void action_ignore(task_info *ti, int signum)
{
   /* do nothing */
}

static void action_continue(task_info *ti, int signum)
{
   NOT_IMPLEMENTED();
}

static void action_stop(task_info *ti, int signum)
{
   NOT_IMPLEMENTED();
}

static const action_type signal_default_actions[32] =
{
   [0] = action_ignore,

   [SIGHUP] = action_terminate,
   [SIGINT] = action_terminate,
   [SIGQUIT] = action_terminate,
   [SIGILL] = action_terminate,
   [SIGABRT] = action_terminate,
   [SIGFPE] = action_terminate,
   [SIGKILL] = action_terminate,
   [SIGSEGV] = action_terminate,
   [SIGPIPE] = action_terminate,
   [SIGALRM] = action_terminate,
   [SIGTERM] = action_terminate,
   [SIGUSR1] = action_terminate,
   [SIGUSR2] = action_terminate,

   [SIGCHLD] = action_ignore,
   [SIGCONT] = action_continue,
   [SIGSTOP] = action_stop,
   [SIGTSTP] = action_stop,
   [SIGTTIN] = action_stop,
   [SIGTTOU] = action_stop,

   [SIGBUS] = action_terminate,
   [SIGPOLL] = action_terminate,
   [SIGPROF] = action_terminate,
   [SIGSYS] = action_terminate,
   [SIGTRAP] = action_terminate,

   [SIGURG] = action_ignore,

   [SIGVTALRM] = action_terminate,
   [SIGXCPU] = action_terminate,
   [SIGXFSZ] = action_terminate
};

void send_signal(task_info *ti, int signum)
{
   ASSERT(0 <= signum && signum < _NSIG);

   if (ti->pi->sa_handlers[signum] == SIG_IGN)
      return;

   /* For the moment, we don't support anything else than SIG_DFL and SIG_IGN */
   ASSERT(ti->pi->sa_handlers[signum] == SIG_DFL);

   action_type action_func =
      signal_default_actions[signum] != NULL
         ? signal_default_actions[signum]
         : action_terminate;

   action_func(ti, signum);
}

struct send_signal_per_task_cb {

   void *tty;
   int signum;

};

static int per_task_cb(void *obj, void *arg)
{
   task_info *ti = obj;
   struct send_signal_per_task_cb *ctx = arg;

   if (!is_kernel_thread(ti) && ti->pi->proc_tty == ctx->tty) {
      printk("send signal to %d [%s]\n", ti->tid, ti->pi->filepath);
      send_signal(ti, ctx->signum);
   }

   return 0;
}

void send_signal_to_tty_processes(void *tty, int signum)
{
   struct send_signal_per_task_cb ctx = { tty, signum };

   disable_preemption();
   {
      iterate_over_tasks(per_task_cb, &ctx);
   }
   enable_preemption();
}

/*
 * -------------------------------------
 * SYSCALLS
 * -------------------------------------
 */

static int
sigaction_int(int signum, const struct k_sigaction *user_act)
{
   task_info *curr = get_curr_task();
   struct k_sigaction act;

   if (copy_from_user(&act, user_act, sizeof(act)) != 0)
      return -EFAULT;

   if (act.sa_flags & SA_SIGINFO) {
      printk("rt_sigaction: SA_SIGINFO not supported");
      return -EINVAL;
   }

   if (act.handler == SIG_DFL || act.handler == SIG_IGN) {

      curr->pi->sa_handlers[signum] = act.handler;
      curr->pi->sa_flags = act.sa_flags;
      memcpy(curr->pi->sa_mask, act.sa_mask, sizeof(act.sa_mask));

   } else {

      printk("rt_sigaction: sa_handler [%p] not supported\n", act.handler);
      return -EINVAL;
   }

   return 0;
}

sptr
sys_rt_sigaction(int signum,
                 const struct k_sigaction *user_act,
                 struct k_sigaction *user_oldact,
                 size_t sigsetsize)
{
   task_info *curr = get_curr_task();
   struct k_sigaction oldact;
   int rc = 0;

   if (signum <= 0 || signum >= _NSIG)
      return -EINVAL;

   if (signum == SIGKILL || signum == SIGSTOP)
      return -EINVAL;

   if (sigsetsize != sizeof(user_act->sa_mask))
      return -EINVAL;

   disable_preemption();
   {
      if (user_oldact != NULL) {

         oldact = (struct k_sigaction) {
            .handler = curr->pi->sa_handlers[signum],
            .sa_flags = curr->pi->sa_flags,
         };

         memcpy(oldact.sa_mask, curr->pi->sa_mask, sizeof(oldact.sa_mask));
      }

      if (user_act != NULL) {
         rc = sigaction_int(signum, user_act);
      }
   }
   enable_preemption();

   if (!rc && user_oldact != NULL) {

      if (copy_to_user(user_oldact, &oldact, sizeof(oldact)) != 0)
         rc = -EFAULT;
   }

   return (sptr)rc;
}

sptr
sys_rt_sigprocmask(int how,
                   sigset_t *set,
                   sigset_t *oset,
                   size_t sigsetsize)
{
   // TODO: implement sys_rt_sigprocmask
   // printk("rt_sigprocmask\n");
   return 0;
}

sptr sys_sigprocmask(uptr a1, uptr a2, uptr a3)
{
   NOT_IMPLEMENTED(); // deprecated interface
}

sptr sys_sigaction(uptr a1, uptr a2, uptr a3)
{
   NOT_IMPLEMENTED(); // deprecated interface
}
