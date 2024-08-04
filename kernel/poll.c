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

      fds[i].revents = 0;
      fs_handle h = get_fs_handle(fds[i].fd);

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
   int cnt = 0;
   int rc;

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
   struct task *curr = get_curr_task();
   struct multi_obj_waiter *waiter = NULL;
   int ready_fds_cnt = 0;

   if (!(waiter = allocate_mobj_waiter(cond_cnt)))
      return -ENOMEM;

   poll_set_conds(waiter, fds, nfds, cond_cnt);

   if (!timeout) {
      ready_fds_cnt = poll_count_ready_fds(fds, nfds);
      free_mobj_waiter(waiter);
      return ready_fds_cnt;
   }

   if (timeout > 0) {
      u32 ticks = MAX((u32)timeout / (1000 / TIMER_HZ), 1u);
      task_set_wakeup_timer(curr, ticks);
   }

   while (true) {

      disable_preemption();
      prepare_to_wait_on_multi_obj(waiter);
      enter_sleep_wait_state();

      if (pending_signals())
         break;

      if (timeout > 0) {

         if (curr->wobj.type) {

            /* we woke-up because of the timeout */
            wait_obj_reset(&curr->wobj);

         } else {

            /*
             * We woke-up because of a kcond was signaled, but that does NOT
             * mean that even the signaled conditions correspond to ready
             * streams. We have to check that.
             */

            ready_fds_cnt = poll_count_ready_fds(fds, nfds);

            if (!ready_fds_cnt)
               continue; /* No ready streams, we have to wait again. */

            task_cancel_wakeup_timer(curr);
         }

      } else {

         /* No timeout: we woke-up because of a kcond was signaled */

         ready_fds_cnt = poll_count_ready_fds(fds, nfds);

         if (!ready_fds_cnt)
            continue; /* No ready streams, we have to wait again. */
      }

      break;
   }

   free_mobj_waiter(waiter);

   if (pending_signals())
      return -EINTR;

   return ready_fds_cnt;
}

int sys_poll(struct pollfd *user_fds, nfds_t nfds, int timeout)
{
   struct task *curr = get_curr_task();
   struct pollfd *fds = curr->args_copybuf;
   int rc, ready_fds_cnt;
   int cond_cnt = 0;

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
         kernel_sleep((u64)timeout / (1000 / TIMER_HZ));

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

