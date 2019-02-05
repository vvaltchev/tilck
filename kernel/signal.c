/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/signal.h>

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
   ASSERT(0 <= signum && signum <= 31);

   if (ti->pi->signal_actions[signum] == TILCK_SIG_IGNORE)
      return;

   /* For the moment, we don't support anything else than SIG_DFL and SIG_IGN */
   ASSERT(ti->pi->signal_actions[signum] == TILCK_SIG_DEFAULT);

   action_type action_func =
      signal_default_actions[signum] != NULL
         ? signal_default_actions[signum]
         : action_terminate;

   action_func(ti, signum);
}
