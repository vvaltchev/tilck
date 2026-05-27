/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_userlim.h>
#include <tilck/common/basic_defs.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/timer.h>

static int
poll_count_conds(struct pollfd *fds, nfds_t nfds)
{
   int cnt = 0;

   for (nfds_t i = 0; i < nfds; i++) {

      fs_handle h;

      fds[i].revents = 0;
      h = get_fs_handle(fds[i].fd);

      if (!h) {
         fds[i].revents = POLLNVAL; /* invalid file descriptor */
         continue;
      }

      if (fds[i].events & (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI)) {

         fds[i].events |= POLLIN; /* treat all IN events as POLLIN */

         if (vfs_get_rready_cond(h))
            cnt++;
      }

      if (fds[i].events & (POLLOUT | POLLWRNORM | POLLWRBAND)) {

         fds[i].events |= POLLOUT; /* treat all OUT events as POLLOUT */

         if (vfs_get_wready_cond(h))
            cnt++;
      }

      if (fds[i].events & POLL_MSG) {
         /* TODO (future): add support for POLL_MSG */
      }

      if (vfs_get_except_cond(h))
         cnt++; /* poll() automatically listens for exception events */
   }

   return cnt;
}

static void
poll_set_conds(struct multi_obj_waiter *w,
               struct pollfd *fds,
               nfds_t nfds,
               int cond_cnt)
{
   int idx = 0;

   for (nfds_t i = 0; i < nfds; i++) {

      fs_handle h = get_fs_handle(fds[i].fd);

      if (!h) {
         fds[i].revents = POLLNVAL; /* invalid file descriptor */
         continue;
      }

      if (fds[i].events & POLLIN) {

         struct kcond *c = vfs_get_rready_cond(h);

         if (c != NULL) {
            ASSERT(idx < cond_cnt);
            mobj_waiter_set(w, idx++, WOBJ_KCOND, c, &c->wait_list);
         }
      }

      if (fds[i].events & POLLOUT) {

         struct kcond *c = vfs_get_wready_cond(h);

         if (c != NULL) {
            ASSERT(idx < cond_cnt);
            mobj_waiter_set(w, idx++, WOBJ_KCOND, c, &c->wait_list);
         }
      }

      if (true) {

         /*
          * poll() always waits for exceptions: "if (true)" has been used just
          * for symmetry with the other cases
          */

         struct kcond *c = vfs_get_except_cond(h);

         if (c != NULL) {
            ASSERT(idx < cond_cnt);
            mobj_waiter_set(w, idx++, WOBJ_KCOND, c, &c->wait_list);
         }
      }

   }
}

static int
poll_count_ready_fds(struct pollfd *fds, nfds_t nfds)
{
   int rc;
   int cnt = 0;

   for (nfds_t i = 0; i < nfds; i++) {

      fs_handle h = get_fs_handle(fds[i].fd);

      if (!h) {
         fds[i].revents = POLLNVAL; /* invalid file descriptor */
         continue;
      }

      if (fds[i].events & POLLIN) {
         if (vfs_read_ready(h)) {

            fds[i].revents |= POLLIN;
            cnt++;
            continue;
         }
      }

      if (fds[i].events & POLLOUT) {
         if (vfs_write_ready(h)) {

            fds[i].revents |= POLLOUT;
            cnt++;
            continue;
         }
      }

      if (true) { /* just for symmetry */
         if ((rc = vfs_except_ready(h))) {
            fds[i].revents |= rc > 0 ? rc : POLLERR;
            cnt++;
            continue;
         }
      }
   }

   return cnt;
}

static int
poll_wait_on_cond(struct pollfd *fds, nfds_t nfds, int timeout, int cond_cnt)
{
   struct multi_obj_waiter *waiter = NULL;
   int ready_fds_cnt = 0;
   struct task *curr = get_curr_task();
   u64 start_ticks = 0;
   u32 timeout_ticks = 0;

   if (!(waiter = allocate_mobj_waiter(cond_cnt)))
      return -ENOMEM;

   poll_set_conds(waiter, fds, nfds, cond_cnt);

   if (!timeout) {
      ready_fds_cnt = poll_count_ready_fds(fds, nfds);
      free_mobj_waiter(waiter);
      return ready_fds_cnt;
   }

   if (timeout > 0) {
      timeout_ticks = MAX((u32)timeout / (1000 / KRN_TIMER_HZ), 1u);
      start_ticks = get_ticks();
   }

   while (true) {

      /*
       * Check readiness with preemption enabled: the *_ready
       * callbacks (e.g. pipe_read_ready) may take mutexes that
       * can block.
       */
      ready_fds_cnt = poll_count_ready_fds(fds, nfds);
      if (ready_fds_cnt > 0)
         break;

      /*
       * Preempt-disabled section: detect missed signals, arm the
       * timer, and enter sleep. No blocking calls past this point.
       *
       * A kcond may have fired during the readiness check while we
       * were in RUNNING state: the signal was recorded in
       * waiter->signaled_list but could not wake us (we were
       * already running). Re-arm fired elements and re-check.
       */
      disable_preemption();

      if (mobj_waiter_rearm_signaled(waiter)) {
         enable_preemption();
         continue;
      }

      /*
       * Arm the wakeup timer inside the preempt-disabled section
       * so irq_resched cannot consume timer_ready between the
       * arming and enter_sleep_wait_state(). The timer is armed
       * fresh before every sleep and cancelled on kcond wake-up,
       * so it is never alive during the preemption-enabled
       * readiness check above.
       */
      if (timeout > 0) {

         const u64 elapsed = get_ticks() - start_ticks;

         if (elapsed >= timeout_ticks) {
            enable_preemption();
            break;
         }

         task_set_wakeup_timer(curr, timeout_ticks - elapsed);
      }

      prepare_to_wait_on_multi_obj(waiter);
      enter_sleep_wait_state();
      /* enter_sleep_wait_state() leaves preemption enabled */

      if (pending_signals())
         break;

      if (timeout > 0 && curr->wobj.type) {
         wait_obj_reset(&curr->wobj);
         break;
      }

      /*
       * Kcond signal woke us. Cancel the timer and re-arm the
       * fired element so the next readiness check finds us
       * registered on all kcond wait_lists.
       */
      if (timeout > 0)
         task_cancel_wakeup_timer(curr);

      disable_preemption();
      mobj_waiter_rearm_signaled(waiter);
      enable_preemption();
   }

   task_cancel_wakeup_timer(curr);
   free_mobj_waiter(waiter);

   if (pending_signals())
      return -EINTR;

   return ready_fds_cnt;
}

int sys_poll(struct pollfd *user_fds, nfds_t nfds, int timeout)
{
   int rc, ready_fds_cnt;
   int cond_cnt = 0;
   struct task *curr = get_curr_task();
   struct pollfd *fds = curr->args_copybuf;

   if (sizeof(struct pollfd) * nfds > ARGS_COPYBUF_SIZE)
      return -EINVAL;

   if (copy_from_user(fds, user_fds, sizeof(struct pollfd) * nfds))
      return -EFAULT;

   for (u32 i = 0; i < nfds; i++)
      fds[i].revents = 0;

   ready_fds_cnt = poll_count_ready_fds(fds, nfds);

   if (ready_fds_cnt > 0)
      goto end;

   if (timeout != 0)
      cond_cnt = poll_count_conds(fds, nfds);

   if (cond_cnt > 0) {

      if ((rc = poll_wait_on_cond(fds, nfds, timeout, cond_cnt)) < 0)
         return rc;

      ready_fds_cnt = rc;

   } else {

      if (timeout > 0) {
         kernel_sleep((u64)timeout / (1000 / KRN_TIMER_HZ));

         if (pending_signals())
            return -EINTR;
      }

      ready_fds_cnt = poll_count_ready_fds(fds, nfds);
   }

end:
   if (copy_to_user(user_fds, fds, sizeof(struct pollfd) * nfds))
      return -EFAULT;

   return ready_fds_cnt;
}

long sys_ppoll(struct pollfd *ufds, unsigned int nfds,
               struct k_timespec64 *tsp, const sigset_t *sigmask,
               size_t sigsetsize)
{
   // TODO: Add full support for ppoll()

   int rc = 0;

   if (!ufds && !nfds && !tsp && !sigmask && !sigsetsize) {

      disable_preemption();
      rc = sys_pause();
      enable_preemption();
      return rc;

   } else if (!sigmask && (sigsetsize == _NSIG/8)) {

      return sys_poll(ufds, nfds,
               tsp ? (int)(tsp->tv_sec*1000 + tsp->tv_nsec/1000000) : -1);

   } else
      return -ENOSYS;
}

