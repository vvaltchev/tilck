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

/*
 * -------------------------------------
 * SYSCALLS
 * -------------------------------------
 */

sptr
sys_rt_sigaction(int signum,
                 const struct sigaction *user_act,
                 struct sigaction *user_oldact,
                 size_t sigsetsize)
{
   printk("sigaction sig: %d\n", signum);

   if (signum <= 0 || signum >= _NSIG)
      return -EINVAL;

   if (signum == SIGKILL || signum == SIGSTOP)
      return -EINVAL;

   // Why this if fails??
   // if (sigsetsize != sizeof(sigset_t)) {
   //    printk("%u vs %u\n", sigsetsize, sizeof(sigset_t));
   //    printk("NSIG: %u\n", _NSIG);
   //    return -EINVAL;
   // }

   task_info *curr = get_curr_task();
   int rc;

   disable_preemption();

   struct sigaction oldact = (struct sigaction) {
      .sa_handler = curr->pi->sa_handlers[signum],
      .sa_mask = curr->pi->sa_mask,
      .sa_flags = curr->pi->sa_flags
   };


   if (user_act != NULL) {

      struct sigaction act;

      rc = copy_from_user(&act, user_act, sizeof(sigaction));

      if (rc != 0) {
         rc = -EFAULT;
         goto out;
      }

      if (act.sa_flags & SA_SIGINFO) {

         printk("sa_sigaction: %p\n", act.sa_sigaction);

         if (act.sa_sigaction == (void*)0xffffff00) {

            printk("   sa_sigaction: DFL\n");
            curr->pi->sa_handlers[signum] = SIG_DFL;

         } else if (act.sa_sigaction == (void*)0xffffff01) {

            printk("   sa_sigaction: IGN\n");
            curr->pi->sa_handlers[signum] = SIG_IGN;

         } else {
            printk("   sa_sigaction: OTHER\n");
            NOT_IMPLEMENTED();
         }


         curr->pi->sa_mask = act.sa_mask;
         curr->pi->sa_flags = act.sa_flags;

      } else {

         if (act.sa_handler == SIG_DFL || act.sa_handler == SIG_IGN) {

            if (act.sa_handler == SIG_DFL)
               printk("   handler: DFL\n");
            else
               printk("   handler: IGN\n");

            curr->pi->sa_handlers[signum] = act.sa_handler;
            curr->pi->sa_mask = act.sa_mask;
            curr->pi->sa_flags = act.sa_flags;

         } else {
            printk("   handler: OTHER\n");
            NOT_IMPLEMENTED();
         }
      }
   }

   if (user_oldact != NULL) {

      rc = copy_to_user(user_oldact, &oldact, sizeof(sigaction));

      if (rc != 0) {
         rc = -EFAULT;
         goto out;
      }
   }

out:
   enable_preemption();
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
