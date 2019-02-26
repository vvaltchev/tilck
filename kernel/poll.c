/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/process.h>

static void
debug_poll_events_dump(struct pollfd *fd)
{
   printk(NO_PREFIX "( ");

   if (fd->events & POLLIN)
      printk(NO_PREFIX "IN ");

   if (fd->events & POLLRDNORM)
      printk(NO_PREFIX "RDNORM ");

   if (fd->events & POLLRDBAND)
      printk(NO_PREFIX "RDBAND ");

   if (fd->events & POLLPRI)
      printk(NO_PREFIX "PRI ");

   if (fd->events & POLLOUT)
      printk(NO_PREFIX "OUT ");

   if (fd->events & POLLWRNORM)
      printk(NO_PREFIX "WRNORM ");

   if (fd->events & POLLWRBAND)
      printk(NO_PREFIX "WRBAND ");

   if (fd->events & POLL_MSG)
      printk(NO_PREFIX "MSG ");

   printk(NO_PREFIX ") ");
}

static void
debug_poll_args_dump(struct pollfd *fds, u32 nfds, int timeout)
{
   printk("poll(fds: [ ");

   for (u32 i = 0; i < nfds; i++) {
      printk(NO_PREFIX "%d: ", fds[i].fd);
      debug_poll_events_dump(fds + i);
   }

   printk(NO_PREFIX "], timeout: %d ms)\n", timeout);
}

static u32
poll_count_conds(struct pollfd *fds, u32 nfds)
{
   u32 cnt = 0;

   for (u32 i = 0; i < nfds; i++) {

      fds[i].revents = 0;
      fs_handle h = get_fs_handle((u32)fds[i].fd);

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
poll_set_conds(multi_obj_waiter *w, struct pollfd *fds, u32 nfds, u32 cond_cnt)
{
   u32 idx = 0;

   for (u32 i = 0; i < nfds; i++) {

      fs_handle h = get_fs_handle((u32)fds[i].fd);

      if (!h) {
         fds[i].revents = POLLNVAL; /* invalid file descriptor */
         continue;
      }

      if (fds[i].events & POLLIN) {

         kcond *c = vfs_get_rready_cond(h);

         if (c != NULL) {

            ASSERT(idx < cond_cnt);
            mobj_waiter_set(w, idx++, WOBJ_KCOND, c, &c->wait_list);
         }
      }

      if (fds[i].events & POLLOUT) {

         kcond *c = vfs_get_wready_cond(h);

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

         kcond *c = vfs_get_except_cond(h);

         if (c != NULL) {

            ASSERT(idx < cond_cnt);
            mobj_waiter_set(w, idx++, WOBJ_KCOND, c, &c->wait_list);
         }
      }

   }
}

static u32
poll_count_ready_conds(struct pollfd *fds, u32 nfds)
{
   u32 cnt = 0;

   for (u32 i = 0; i < nfds; i++) {

      fs_handle h = get_fs_handle((u32)fds[i].fd);

      if (!h) {
         fds[i].revents = POLLNVAL; /* invalid file descriptor */
         continue;
      }

      if (fds[i].events & POLLIN) {
         if (vfs_read_ready(h)) {

            fds[i].revents |= POLLIN;
            cnt++;
         }
      }

      if (fds[i].events & POLLOUT) {
         if (vfs_write_ready(h)) {

            fds[i].revents |= POLLOUT;
            cnt++;
         }
      }

      if (true) { /* just for symmetry */
         if (vfs_except_ready(h)) {

            fds[i].revents |= POLL_ERR;
            cnt++;
         }
      }
   }

   return cnt;
}

static int
poll_wait_on_cond(struct pollfd *fds, u32 nfds, int timeout, u32 cond_cnt)
{
   task_info *curr = get_curr_task();
   multi_obj_waiter *waiter = NULL;
   int rc = 0;

   if (!(waiter = allocate_mobj_waiter(cond_cnt)))
      return -ENOMEM;

   poll_set_conds(waiter, fds, nfds, cond_cnt);

   if (timeout > 0) {

      u32 ticks = (u32)timeout / (1000 / TIMER_HZ);

      if (!ticks) {

         /*
          * In case the timeout value is less than 1 tick, just behave as if
          * the timeout was 0.
          */

         poll_count_ready_conds(fds, nfds);
         free_mobj_waiter(waiter);
         return 0;
      }

      task_set_wakeup_timer(curr, ticks);
   }

   while (true) {

      kernel_sleep_on_waiter(waiter);

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

            if (!poll_count_ready_conds(fds, nfds))
               continue; /* No ready streams, we have to wait again. */
         }

      } else {

         /* No timeout: we woke-up because of a kcond was signaled */

         if (!poll_count_ready_conds(fds, nfds))
            continue; /* No ready streams, we have to wait again. */
      }

      break;
   }

   free_mobj_waiter(waiter);
   return rc;
}

sptr sys_poll(struct pollfd *user_fds, nfds_t user_nfds, int timeout)
{
   const u32 nfds = (u32) user_nfds;
   task_info *curr = get_curr_task();
   struct pollfd *fds = curr->args_copybuf;
   u32 cond_cnt = 0;
   int rc;

   if (sizeof(struct pollfd) * nfds > ARGS_COPYBUF_SIZE)
      return -EINVAL;

   if (copy_from_user(fds, user_fds, sizeof(struct pollfd) * nfds))
      return -EFAULT;

   debug_poll_args_dump(fds, nfds, timeout);

   if (timeout != 0)
      cond_cnt = poll_count_conds(fds, nfds);

   if (cond_cnt > 0) {

      if ((rc = poll_wait_on_cond(fds, nfds, timeout, cond_cnt)))
         return rc;

   } else {

      if (timeout > 0) {
         kernel_sleep((u64)timeout / (1000 / TIMER_HZ));
      }
   }

   if (copy_to_user(user_fds, fds, sizeof(struct pollfd) * nfds))
      return -EFAULT;

   return 0;
}
