/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/sys_types.h>
#include <tilck/kernel/hal.h>

#include <tilck/mods/tracing.h>

typedef void (*action_type)(struct task *, int signum);

static void add_pending_sig(struct task *ti, int signum)
{
   ASSERT(signum > 0);
   signum--;

   int slot = signum / NBITS;
   int index = signum % NBITS;

   if (slot >= K_SIGACTION_MASK_WORDS)
      return; /* just silently ignore signals that we don't support */

   ti->pending_signums[slot] |= (1 << index);
}

static void del_pending_sig(struct task *ti, int signum)
{
   ASSERT(signum > 0);
   signum--;

   int slot = signum / NBITS;
   int index = signum % NBITS;

   if (slot >= K_SIGACTION_MASK_WORDS)
      return; /* just silently ignore signals that we don't support */

   ti->pending_signums[slot] &= ~(1 << index);
}

static bool is_pending_sig(struct task *ti, int signum)
{
   ASSERT(signum > 0);
   signum--;

   int slot = signum / NBITS;
   int index = signum % NBITS;

   if (slot >= K_SIGACTION_MASK_WORDS)
      return false; /* just silently ignore signals that we don't support */

   return !!(ti->pending_signums[slot] & (1 << index));
}

static bool is_sig_masked(struct task *ti, int signum)
{
   ASSERT(signum > 0);
   signum--;

   int slot = signum / NBITS;
   int index = signum % NBITS;

   if (slot >= K_SIGACTION_MASK_WORDS)
      return true; /* signals we don't support are always "masked" */

   return !!(ti->sa_mask[slot] & (1 << index));
}

static int get_first_pending_sig(struct task *ti)
{
   for (u32 i = 0; i < K_SIGACTION_MASK_WORDS; i++) {

      ulong val = ti->pending_signums[i];

      if (val != 0) {
         u32 idx = get_first_set_bit_index_l(val);
         int signum = (int)(i * NBITS + idx + 1);

         if (!is_sig_masked(ti, signum))
            return signum;
      }
   }

   return -1;
}

void drop_all_pending_signals(void *__curr)
{
   ASSERT(!is_preemption_enabled());
   struct task *ti = __curr;

   for (u32 i = 0; i < K_SIGACTION_MASK_WORDS; i++) {
      ti->pending_signums[i] = 0;
   }
}

void reset_all_custom_signal_handlers(void *__curr)
{
   ASSERT(!is_preemption_enabled());
   struct task *ti = __curr;
   struct process *pi = ti->pi;

   for (u32 i = 1; i < _NSIG; i++) {

      if (pi->sa_handlers[i-1] != SIG_DFL && pi->sa_handlers[i-1] != SIG_IGN)
         pi->sa_handlers[i-1] = SIG_DFL;
   }
}

static void kill_task_now_or_later(struct task *ti, void *regs, int signum)
{
   if (ti == get_curr_task()) {

      /* We can terminate the task immediately */
      enable_preemption();
      terminate_process(0, signum);
      NOT_REACHED();

   } else {

      /* We have to setup a trampoline to any syscall */
      setup_pause_trampoline(regs);
   }
}

bool process_signals(void *__ti, enum sig_state sig_state, void *regs)
{
   ASSERT(!is_preemption_enabled());
   struct task *ti = __ti;
   int sig;

   ASSERT(ti == get_curr_task() || sig_state == sig_in_usermode);

   if (is_pending_sig(ti, SIGKILL)) {

      /*
       * SIGKILL will always have absolute priority over anything else: no
       * matter if there are other pending signals or we're already running
       * a custom signal handler.
       */

      kill_task_now_or_later(ti, regs, SIGKILL);
      return true;
   }

   if (ti->nested_sig_handlers > 0 && sig_state != sig_in_restart_syscall) {
      /*
       * For the moment, in Tilck only signal handlers (even of different types)
       * will not be able to interrupt each other. This is the equivalent of
       * having each sigaction's sa_mask = 0xffffffff[...].
       */
      return false;
   }

   sig = get_first_pending_sig(ti);

   if (sig < 0)
      return false;

   trace_signal_delivered(ti->tid, sig);
   __sighandler_t handler = ti->pi->sa_handlers[sig - 1];

   if (handler) {

      trace_printk(10, "Setup signal handler %p for TID %d for signal %s[%d]",
                   handler, ti->tid, get_signal_name(sig), sig);

      del_pending_sig(ti, sig);
      setup_sig_handler(ti, sig_state, regs, (ulong)handler, sig);

   } else {

      /*
       * If we got here, there is no registered custom handler for the signal,
       * the signal has not been ignored explicitly and the default action for
       * the signal is terminate.
       */

      kill_task_now_or_later(ti, regs, sig);
   }

   return true;
}

static void signal_wakeup_task(struct task *ti)
{
   if (!ti->vfork_stopped) {

      if (ti->state == TASK_STATE_SLEEPING) {

         /*
          * We must NOT wake up tasks waiting on a mutex or on a semaphore:
          * supporting spurious wake-ups there, is just a waste of resources.
          * On the contrary, if a task is waiting on a condition or sleeping
          * in kernel_sleep(), we HAVE to wake it up.
          */

         if (ti->wobj.type != WOBJ_KMUTEX && ti->wobj.type != WOBJ_SEM)
            wake_up(ti);
      }


      ti->stopped = false;

   } else {

      /*
       * The task is vfork_stopped: we cannot make it runnable, nor kill it
       * right now. Just registering the signal as pending is enough. As soon
       * as the process wakes up, the killing signal will be delivered.
       * Supporting the killing a of vforked process (while its child is still
       * alive and has not called execve()) is just too tricky.
       *
       * TODO: consider supporting killing of vforked process.
       */
   }
}

static void action_terminate(struct task *ti, int signum)
{
   ASSERT(!is_preemption_enabled());
   ASSERT(!is_kernel_thread(ti));

   if (ti == get_curr_task()) {

      enable_preemption();
      ASSERT(is_preemption_enabled());

      terminate_process(0, signum);
      NOT_REACHED();
   }

   add_pending_sig(ti, signum);
   signal_wakeup_task(ti);
}

static void action_ignore(struct task *ti, int signum)
{
   if (ti->tid == 1) {
      printk(
         "WARNING: ignoring signal %s[%d] sent to init (pid 1)\n",
         get_signal_name(signum), signum
      );
   }
}

static void action_stop(struct task *ti, int signum)
{
   ASSERT(!is_kernel_thread(ti));

   trace_signal_delivered(ti->tid, signum);
   ti->stopped = true;
   ti->wstatus = STOPCODE(signum);
   wake_up_tasks_waiting_on(ti, task_stopped);

   if (ti == get_curr_task()) {
      kernel_yield_preempt_disabled();
   }
}

static void action_continue(struct task *ti, int signum)
{
   ASSERT(!is_kernel_thread(ti));

   if (ti->vfork_stopped)
      return;

   trace_signal_delivered(ti->tid, signum);
   ti->stopped = false;
   ti->wstatus = CONTINUED;
   wake_up_tasks_waiting_on(ti, task_continued);
}

static const action_type signal_default_actions[_NSIG] =
{
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
   [SIGWINCH] = action_terminate,
};

static void do_send_signal(struct task *ti, int signum)
{
   ASSERT(IN_RANGE(signum, 0, _NSIG));

   if (signum == 0) {

      /*
       * Do nothing, but don't treat it as an error.
       *
       * From kill(2):
       *    If sig is 0, then no signal is sent, but error checking is still
       *    performed; this can be used to check for the existence of a
       *    process ID or process group ID.
       */
      return;
   }

   if (signum >= _NSIG)
      return; /* ignore unknown and unsupported signal */

   if (ti->nested_sig_handlers < 0)
      return; /* the task is dying, no signals allowed */

   __sighandler_t h = ti->pi->sa_handlers[signum - 1];

   if (h == SIG_IGN) {

      action_ignore(ti, signum);

   } else if (h == SIG_DFL) {

      action_type action_func =
         signal_default_actions[signum] != NULL
            ? signal_default_actions[signum]
            : action_terminate;

      if (action_func)
         action_func(ti, signum);

   } else {

      add_pending_sig(ti, signum);
      signal_wakeup_task(ti);
   }
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

   if (ti->state == TASK_STATE_ZOMBIE)
      goto end; /* do nothing */

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

   if (act.sa_flags & SA_NOCLDSTOP) {
      return -EINVAL; /* not supported */
   }

   if (act.sa_flags & SA_NOCLDWAIT) {
      return -EINVAL; /* not supported */
   }

   if (act.sa_flags & SA_SIGINFO) {
      return -EINVAL; /* not supported */
   }

   if (act.sa_flags & SA_ONSTACK) {
      return -EINVAL; /* not supported */
   }

   if (act.sa_flags & SA_RESETHAND) {
      /* TODO: add support for this simple flag */
   }

   if (act.sa_flags & SA_NODEFER) {

      /*
       * Just ignore this. For the moment, Tilck will block the delivery of
       * signals with custom handlers, if ANY signal handler is running.
       */
   }

   if (act.sa_flags & SA_RESTART) {
      /* For the moment, silently signore this important flag too. */
   }

   curr->pi->sa_handlers[signum - 1] = act.handler;
   return 0;
}

int
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
            .handler = curr->pi->sa_handlers[signum - 1],
            .sa_flags = 0,
         };

         memcpy(oldact.sa_mask, curr->sa_mask, sizeof(oldact.sa_mask));
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

   return rc;
}

static int
__sys_rt_sigprocmask(int how,
                     sigset_t *user_set,
                     sigset_t *user_oldset,
                     size_t sigsetsize)
{
   struct task *ti = get_curr_task();
   int rc;

   if (user_oldset) {

      rc = copy_to_user(user_oldset, ti->sa_mask, sigsetsize);

      if (rc)
         return -EFAULT;

      if (sigsetsize > sizeof(ti->sa_mask)) {

         const size_t diff = sigsetsize - sizeof(ti->sa_mask);

         rc = copy_to_user(
            (char *)user_oldset + sizeof(ti->sa_mask),
            zero_page,
            diff
         );

         if (rc)
            return -EFAULT;
      }
   }

   if (user_set) {

      for (u32 i = 0; i < K_SIGACTION_MASK_WORDS; i++) {

         ulong w = 0;

         rc = copy_from_user(
            &w,
            (char *)user_set + i * sizeof(ulong),
            sizeof(ulong)
         );

         if (rc)
            return -EFAULT;

         switch (how) {

            case SIG_BLOCK:
               ti->sa_mask[i] |= w;
               break;

            case SIG_UNBLOCK:
               ti->sa_mask[i] &= ~w;
               break;

            case SIG_SETMASK:
               ti->sa_mask[i] = w;
               break;

            default:
               return -EINVAL;
         }
      }
   }

   return 0;
}

int
sys_rt_sigprocmask(int how,
                   sigset_t *user_set,
                   sigset_t *user_oldset,
                   size_t sigsetsize)
{
   int rc;
   disable_preemption();
   {
      rc = __sys_rt_sigprocmask(how, user_set, user_oldset, sigsetsize);
   }
   enable_preemption();
   return rc;
}

static int
__sys_rt_sigpending(sigset_t *u_set, size_t sigsetsize)
{
   struct task *ti = get_curr_task();
   int rc;

   if (!u_set)
      return 0;

   rc = copy_to_user(u_set, ti->pending_signums, sigsetsize);

   if (rc)
      return -EFAULT;

   if (sigsetsize > sizeof(ti->pending_signums)) {

      const size_t diff = sigsetsize - sizeof(ti->pending_signums);

      rc = copy_to_user(
         (char *)u_set + sizeof(ti->pending_signums),
         zero_page,
         diff
      );

      if (rc)
         return -EFAULT;
   }

   return 0;
}

int
sys_rt_sigpending(sigset_t *u_set, size_t sigsetsize)
{
   int rc;
   disable_preemption();
   {
      rc = __sys_rt_sigpending(u_set, sigsetsize);
   }
   enable_preemption();
   return rc;
}

int sys_pause(void)
{
   task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
   kernel_yield();

   if (pending_signals())
      return -EINTR;

   return 0;
}

int sys_sigprocmask(ulong a1, ulong a2, ulong a3)
{
   NOT_IMPLEMENTED(); // deprecated interface
}

int sys_sigaction(ulong a1, ulong a2, ulong a3)
{
   NOT_IMPLEMENTED(); // deprecated interface
}

__sighandler_t sys_signal(int signum, __sighandler_t handler)
{
   NOT_IMPLEMENTED(); // deprecated interface
}
