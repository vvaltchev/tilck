/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/sys_types.h>

typedef void (*action_type)(struct task *, int signum);

static void action_terminate(struct task *ti, int signum)
{
   terminate_process(ti, 0, signum);
}

static void action_ignore(struct task *ti, int signum)
{
   /* do nothing */
}

static void action_stop(struct task *ti, int signum)
{
   ti->stopped = true;
   ti->exit_wstatus = STOPCODE(signum);
   wake_up_tasks_waiting_on(ti);

   if (ti == get_curr_task()) {
      enable_preemption();
      schedule_outside_interrupt_context();
   }
}

static void action_continue(struct task *ti, int signum)
{
   ti->stopped = false;
   ti->exit_wstatus = CONTINUED;
   wake_up_tasks_waiting_on(ti);
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
   [SIGXFSZ] = action_terminate,
};

static void do_send_signal(struct task *ti, int signum)
{
   ASSERT(IN_RANGE(signum, 0, _NSIG));
   __sighandler_t h = ti->pi->sa_handlers[signum];

   if (h == SIG_IGN)
      return; /* the signal must be just ignored */

   if (h != SIG_DFL) {

      /* DIRTY HACK: treat custom signal handlers as SIG_IGN */
      return;
   }

   /*
    * For the moment, we don't support anything else than SIG_DFL and SIG_IGN.
    * TODO: actually support custom signal handlers.
    */
   ASSERT(h == SIG_DFL);

   action_type action_func =
      signal_default_actions[signum] != NULL
         ? signal_default_actions[signum]
         : action_terminate;

   action_func(ti, signum);
}

int send_signal2(int pid, int tid, int signum, bool whole_process)
{
   struct task *ti;
   int rc = -ESRCH;

   disable_preemption();

   if (!(ti = get_task(tid)))
      goto err_end;

   if (is_kernel_thread(ti))
      goto err_end; /* cannot send signals to kernel threads */

   /* When `whole_process` is true, tid must be == pid */
   if (whole_process && ti->pi->pid != tid)
      goto err_end;

   if (ti->pi->pid != pid)
      goto err_end;

   if (signum == 0)
      goto end; /* the user app is just checking permissions */

   /* TODO: update this code when thread support is added */
   do_send_signal(ti, signum);

end:
   rc = 0;

err_end:
   enable_preemption();
   return rc;
}

/*
 * -------------------------------------
 * SYSCALLS
 * -------------------------------------
 */

static int
sigaction_int(int signum, const struct k_sigaction *user_act)
{
   struct task *curr = get_curr_task();
   struct k_sigaction act;

   if (copy_from_user(&act, user_act, sizeof(act)) != 0)
      return -EFAULT;

   if (act.sa_flags & SA_SIGINFO) {
      //printk("rt_sigaction: SA_SIGINFO not supported");
      return -EINVAL;
   }

   // TODO: actually support custom signal handlers

   if (act.handler != SIG_DFL && act.handler != SIG_IGN) {

      // printk("rt_sigaction: sa_handler [%p] not supported\n", act.handler);
      // return -EINVAL;
   }

   curr->pi->sa_handlers[signum] = act.handler;
   curr->pi->sa_flags = act.sa_flags;
   memcpy(curr->pi->sa_mask, act.sa_mask, sizeof(act.sa_mask));
   return 0;
}

sptr
sys_rt_sigaction(int signum,
                 const struct k_sigaction *user_act,
                 struct k_sigaction *user_oldact,
                 size_t sigsetsize)
{
   struct task *curr = get_curr_task();
   struct k_sigaction oldact;
   int rc = 0;

   if (!IN_RANGE(signum, 1, _NSIG))
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

int sys_sigprocmask(uptr a1, uptr a2, uptr a3)
{
   NOT_IMPLEMENTED(); // deprecated interface
}

int sys_sigaction(uptr a1, uptr a2, uptr a3)
{
   NOT_IMPLEMENTED(); // deprecated interface
}
