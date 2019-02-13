/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/process.h>

static void
debug_dump_fds(const char *name, int nfds, fd_set *s)
{
   if (s) {

      printk("    %s: [ ", name);

      for (int i = 0; i < nfds; i++)
         if (FD_ISSET(i, s))
            printk(NO_PREFIX "%d ", i);

      printk(NO_PREFIX "]\n");

   } else {
      printk("    %s: NULL,\n", name);
   }
}

static void
debug_dump_select_args(int nfds, fd_set *rfds, fd_set *wfds,
                       fd_set *efds, struct timeval *tv)
{
   printk("sys_select(\n");
   printk("    nfds: %d,\n", nfds);

   debug_dump_fds("rfds", nfds, rfds);
   debug_dump_fds("wfds", nfds, wfds);
   debug_dump_fds("efds", nfds, efds);

   if (tv)
      printk("    tv: %u secs, %u usecs\n", tv->tv_sec, tv->tv_usec);
   else
      printk("    tv: NULL\n");

   printk(")\n");
}

static int
select_count_kcond(u32 nfds,
                   fd_set *set,
                   u32 *kcond_count_ref,
                   func_get_rwe_cond get_cond)
{
   if (!set)
      return 0;

   for (u32 i = 0; i < nfds; i++) {

      if (!FD_ISSET(i, set))
         continue;

      fs_handle h = get_fs_handle(i);

      if (!h)
         return -EBADF;

      if (get_cond(h))
         (*kcond_count_ref)++;
   }

   return 0;
}

static void
select_set_kcond(u32 nfds,
                 multi_obj_waiter *w,
                 u32 *curr_i,
                 fd_set *set,
                 func_get_rwe_cond get_cond)
{
   if (!set)
      return;

   for (u32 i = 0; i < nfds; i++) {

      if (!FD_ISSET(i, set))
         continue;

      kcond *c;
      fs_handle h = get_fs_handle(i);
      ASSERT(h != NULL);

      c = get_cond(h);
      ASSERT((*curr_i) < w->count);

      if (c)
         mobj_waiter_set(w, (*curr_i)++, WOBJ_KCOND, c, &c->wait_list);
   }
}

static int
select_set_ready(u32 nfds, fd_set *set, func_rwe_ready is_ready)
{
   int tot = 0;

   if (!set)
      return tot;

   for (u32 i = 0; i < nfds; i++) {

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

sptr sys_select(int nfds, fd_set *user_rfds, fd_set *user_wfds,
                fd_set *user_efds, struct timeval *user_tv)
{
   static const func_get_rwe_cond gcf[3] = {
      &vfs_get_rready_cond,
      &vfs_get_wready_cond,
      &vfs_get_except_cond
   };

   static const func_rwe_ready grf[3] = {
      &vfs_read_ready,
      &vfs_write_ready,
      &vfs_except_ready
   };

   fd_set *u_sets[3] = { user_rfds, user_wfds, user_efds };

   task_info *curr = get_curr_task();
   multi_obj_waiter *waiter = NULL;
   int total_ready_count = 0;
   struct timeval *tv = NULL;
   fd_set *sets[3] = {0};
   u32 kcond_count = 0;
   u64 timeout_ticks = 0;
   int rc;

   if (nfds < 0 || nfds > MAX_HANDLES)
      return -EINVAL;

   for (int i = 0; i < 3; i++) {

      if (!u_sets[i])
         continue;

      sets[i] = ((fd_set*) curr->args_copybuf) + i;

      if (copy_from_user(sets[i], u_sets[i], sizeof(fd_set)))
         return -EFAULT;
   }

   if (user_tv) {

      tv = (void *) ((fd_set *) curr->args_copybuf) + 3;

      if (copy_from_user(tv, user_tv, sizeof(struct timeval)))
         return -EFAULT;

      timeout_ticks += (u64)tv->tv_sec * TIMER_HZ;
      timeout_ticks += (u64)tv->tv_usec / (1000000ull / TIMER_HZ);

      /* NOTE: select() can't sleep for more than UINT32_MAX ticks */
      timeout_ticks = MIN(timeout_ticks, UINT32_MAX);
   }

   //debug_dump_select_args(nfds, sets[0], sets[1], sets[2], tv);

   if (!tv || timeout_ticks > 0) {
      for (int i = 0; i < 3; i++) {
         if ((rc = select_count_kcond((u32)nfds, sets[i],
                                      &kcond_count, gcf[i])))
         {
            return rc;
         }
      }
   }

   if (kcond_count > 0) {

      u32 curr_i = 0;

      /*
       * NOTE: it is not that difficult kcond_count to be 0: it's enough the
       * specified files to NOT have r/w/e get kcond functions.
       */

      if (!(waiter = allocate_mobj_waiter(kcond_count)))
         return -ENOMEM;

      for (int i = 0; i < 3; i++) {
         select_set_kcond((u32)nfds, waiter, &curr_i, sets[i], gcf[i]);
      }

      if (tv) {
         ASSERT(timeout_ticks > 0);
         task_set_wakeup_timer(get_curr_task(), (u32)timeout_ticks);
      }

      kernel_sleep_on_waiter(waiter);

      if (tv) {

         if (curr->wobj.type) {

            /* we woke-up because of the timeout */
            wait_obj_reset(&curr->wobj);
            tv->tv_sec = 0;
            tv->tv_usec = 0;

         } else {

            /* we woke-up because of a kcond was signaled */
            u32 rem = task_cancel_wakeup_timer(curr);
            tv->tv_sec = rem / TIMER_HZ;
            tv->tv_usec = (rem % TIMER_HZ) * (1000000 / TIMER_HZ);
         }
      }

      /* OK, we woke-up: it does not matter which kcond was signaled */
      free_mobj_waiter(waiter);

   } else {

      if (timeout_ticks > 0) {

         /*
          * Corner case: no conditions on which to wait, but timeout is > 0:
          * this is still a valid case. Many years ago the following call:
          *    select(0, NULL, NULL, NULL, &tv)
          * was even used as a portable implementation of nanosleep().
          */

         kernel_sleep(timeout_ticks);
      }
   }

   for (int i = 0; i < 3; i++) {

      total_ready_count += select_set_ready((u32)nfds, sets[i], grf[i]);

      if (u_sets[i] && copy_to_user(u_sets[i], sets[i], sizeof(fd_set)))
         return -EFAULT;
   }

   if (tv && copy_to_user(user_tv, tv, sizeof(struct timeval)))
      return -EFAULT;

   return total_ready_count;
}
