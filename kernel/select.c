/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/fs/vfs.h>

struct select_ctx {
   int nfds;
   fd_set *sets[3];
   fd_set *u_sets[3];
   struct timeval *tv;
   struct timeval *user_tv;
   u32 cond_cnt;
   u32 timeout_ticks;
};

static const func_get_rwe_cond gcf[3] = {
   &vfs_get_rready_cond,
   &vfs_get_wready_cond,
   &vfs_get_except_cond
};

static const func_rwe_ready grf[3] = {
   &vfs_read_ready,
   &vfs_write_ready,
   &vfs_except_ready,
};

static int
select_count_cond_per_set(struct select_ctx *c,
                          fd_set *set,
                          func_get_rwe_cond gcfunc)
{
   if (!set)
      return 0;

   for (int i = 0; i < c->nfds; i++) {

      if (!FD_ISSET(i, set))
         continue;

      fs_handle h = get_fs_handle(i);

      if (!h)
         return -EBADF;

      if (gcfunc(h))
         c->cond_cnt++;
   }

   return 0;
}

static int
select_set_kcond(int nfds,
                 struct multi_obj_waiter *w,
                 u32 *idx,
                 fd_set *set,
                 func_get_rwe_cond get_cond)
{
   fs_handle h;
   struct kcond *c;

   if (!set)
      return 0;

   for (int i = 0; i < nfds; i++) {

      if (!FD_ISSET(i, set))
         continue;

      if (!(h = get_fs_handle(i)))
         return -EBADF;

      c = get_cond(h);

      if (c) {
         ASSERT((*idx) < w->count);
         mobj_waiter_set(w, (*idx)++, WOBJ_KCOND, c, &c->wait_list);
      }
   }

   return 0;
}

static int
select_set_ready(int nfds, fd_set *set, func_rwe_ready is_ready)
{
   int tot = 0;

   if (!set)
      return tot;

   for (int i = 0; i < nfds; i++) {

      if (!FD_ISSET(i, set))
         continue;

      fs_handle h = get_fs_handle(i);

      if (!h || !is_ready(h)) {
         FD_CLR(i, set);
      } else {
         tot++;
      }
   }

   return tot;
}

static int
count_ready_streams_per_set(int nfds, fd_set *set, func_rwe_ready is_ready)
{
   int count = 0;

   if (!set)
      return count;

   for (int j = 0; j < nfds; j++) {

      if (!FD_ISSET(j, set))
         continue;

      fs_handle h = get_fs_handle(j);

      if (h && is_ready(h))
         count++;
   }

   return count;
}

static int
count_ready_streams(int nfds, fd_set *sets[3])
{
   int count = 0;

   for (int i = 0; i < 3; i++) {
      count += count_ready_streams_per_set(nfds, sets[i], grf[i]);
   }

   return count;
}

static int
select_wait_on_cond(struct select_ctx *c)
{
   struct task *curr = get_curr_task();
   struct multi_obj_waiter *waiter = NULL;
   u32 idx = 0;
   int rc = 0;

   if (!(waiter = allocate_mobj_waiter(c->cond_cnt)))
      return -ENOMEM;

   for (int i = 0; i < 3; i++) {
      if ((rc = select_set_kcond(c->nfds, waiter, &idx, c->sets[i], gcf[i])))
         goto out;
   }

   if (c->tv) {
      ASSERT(c->timeout_ticks > 0);
      task_set_wakeup_timer(curr, c->timeout_ticks);
   }

   while (true) {

      kernel_sleep_on_waiter(waiter);

      if (pending_signals())
         break;

      if (c->tv) {

         if (curr->wobj.type) {

            /* we woke-up because of the timeout */
            wait_obj_reset(&curr->wobj);
            c->tv->tv_sec = 0;
            c->tv->tv_usec = 0;

         } else {

            /*
             * We woke-up because of a kcond was signaled, but that does NOT
             * mean that even the signaled conditions correspond to ready
             * streams. We have to check that.
             */

            if (!count_ready_streams(c->nfds, c->sets))
               continue; /* No ready streams, we have to wait again. */

            u32 rem = task_cancel_wakeup_timer(curr);
            c->tv->tv_sec = rem / TIMER_HZ;
            c->tv->tv_usec = (rem % TIMER_HZ) * (1000000 / TIMER_HZ);
         }

      } else {

         /* No timeout: we woke-up because of a kcond was signaled */

         if (!count_ready_streams(c->nfds, c->sets))
            continue; /* No ready streams, we have to wait again. */
      }

      /* count_ready_streams() returned > 0 */
      break;
   }

out:
   free_mobj_waiter(waiter);

   if (pending_signals())
      return -EINTR;

   return rc;
}

static int
select_read_user_sets(fd_set *sets[3], fd_set *u_sets[3])
{
   struct task *curr = get_curr_task();

   for (int i = 0; i < 3; i++) {

      if (!u_sets[i])
         continue;

      sets[i] = ((fd_set*) curr->args_copybuf) + i;

      if (copy_from_user(sets[i], u_sets[i], sizeof(fd_set)))
         return -EFAULT;
   }

   return 0;
}

static int
select_read_user_tv(struct timeval *user_tv,
                    struct timeval **tv_ref,
                    u32 *timeout)
{
   struct task *curr = get_curr_task();
   struct timeval *tv = NULL;

   if (user_tv) {

      tv = (void *) ((fd_set *) curr->args_copybuf) + 3;

      if (copy_from_user(tv, user_tv, sizeof(struct timeval)))
         return -EFAULT;

      u64 tmp = 0;
      tmp += (u64)tv->tv_sec * TIMER_HZ;
      tmp += (u64)tv->tv_usec / (1000000 / TIMER_HZ);

      /* NOTE: select() can't sleep for more than UINT32_MAX ticks */
      *timeout = (u32) MIN(tmp, UINT32_MAX);

      if (*timeout == 0) {

         /*
          * In case the timeout value is less than 1 tick, just behave as if
          * the timeout was 0.
          */

         tv = NULL;
      }
   }

   *tv_ref = tv;
   return 0;
}

static int
select_compute_cond_cnt(struct select_ctx *c)
{
   int rc;

   if (!c->tv || c->timeout_ticks > 0) {
      for (int i = 0; i < 3; i++) {
         if ((rc = select_count_cond_per_set(c, c->sets[i], gcf[i])))
            return rc;
      }
   }

   return 0;
}

static int
select_write_user_sets(struct select_ctx *c)
{
   fd_set **sets = c->sets;
   fd_set **u_sets = c->u_sets;
   int total_ready_count = 0;

   for (int i = 0; i < 3; i++) {

      total_ready_count += select_set_ready(c->nfds, sets[i], grf[i]);

      if (u_sets[i] && copy_to_user(u_sets[i], sets[i], sizeof(fd_set)))
         return -EFAULT;
   }

   if (c->tv && copy_to_user(c->user_tv, c->tv, sizeof(struct timeval)))
      return -EFAULT;

   return total_ready_count;
}

int sys_select(int user_nfds,
               fd_set *user_rfds,
               fd_set *user_wfds,
               fd_set *user_efds,
               struct timeval *user_tv)
{
   struct select_ctx ctx = (struct select_ctx) {

      .nfds = user_nfds,
      .sets = { 0 },
      .u_sets = { user_rfds, user_wfds, user_efds },
      .tv = NULL,
      .user_tv = user_tv,
      .cond_cnt = 0,
      .timeout_ticks = 0,
   };

   int rc;

   if (user_nfds < 0 || user_nfds > MAX_HANDLES)
      return -EINVAL;

   if ((rc = select_read_user_sets(ctx.sets, ctx.u_sets)))
      return rc;

   if ((rc = select_read_user_tv(user_tv, &ctx.tv, &ctx.timeout_ticks)))
      return rc;

   if ((rc = count_ready_streams(ctx.nfds, ctx.sets)) > 0)
      return select_write_user_sets(&ctx);

   if ((rc = select_compute_cond_cnt(&ctx)))
      return rc;

   if (ctx.cond_cnt > 0 && (!user_tv || ctx.timeout_ticks > 0)) {

      /*
       * The count of condition variables for all the file descriptors is
       * greater than 0. That's typical.
       */

      if ((rc = select_wait_on_cond(&ctx)))
         return rc;

   } else {

      /*
       * It is not that difficult cond_cnt to be 0: it's enough the specified
       * files to NOT have r/w/e get kcond functions. Also, all the sets might
       * be NULL (see the comment below).
       */

      if (ctx.timeout_ticks > 0) {

         /*
          * Corner case: no conditions on which to wait, but timeout is > 0:
          * this is still a valid case. Many years ago the following call:
          *    select(0, NULL, NULL, NULL, &tv)
          * was even used as a portable implementation of nanosleep().
          */

         kernel_sleep(ctx.timeout_ticks);

         if (pending_signals())
            return -EINTR;
      }
   }

   return select_write_user_sets(&ctx);
}
