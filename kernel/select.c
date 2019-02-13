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
                       fd_set *efds, struct timeval *tout)
{
   printk("sys_select(\n");
   printk("    nfds: %d,\n", nfds);

   debug_dump_fds("rfds", nfds, rfds);
   debug_dump_fds("wfds", nfds, wfds);
   debug_dump_fds("efds", nfds, efds);

   if (tout)
      printk("    tout: %u secs, %u usecs\n", tout->tv_sec, tout->tv_usec);
   else
      printk("    tout: NULL\n");

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

sptr sys_select(int nfds, fd_set *user_rfds, fd_set *user_wfds,
                fd_set *user_efds, struct timeval *user_tout)
{
   static func_get_rwe_cond gcf[3] = {
      &vfs_get_rready_cond,
      &vfs_get_wready_cond,
      &vfs_get_except_cond
   };

   fd_set *u_sets[3] = { user_rfds, user_wfds, user_efds };

   task_info *curr = get_curr_task();
   multi_obj_waiter *waiter = NULL;
   struct timeval *tout = NULL;
   fd_set *sets[3] = {0};
   u32 kcond_count = 0;
   int rc;

   if (nfds < 0 || nfds > MAX_HANDLES)
      return -EINVAL;

   for (int i = 0; i < 3; i++) {

      if (!u_sets[i])
         continue;

      sets[i] = ((fd_set*) curr->args_copybuf) + i;

      if (copy_from_user(sets[i], u_sets[i], sizeof(fd_set)))
         return -EBADF;
   }

   if (user_tout) {

      tout = (void *) ((fd_set *) curr->args_copybuf) + 3;

      if (copy_from_user(tout, user_tout, sizeof(struct timeval)))
         return -EBADF;
   }

   debug_dump_select_args(nfds, sets[0], sets[1], sets[2], tout);

   for (int i = 0; i < 3; i++) {
      if ((rc = select_count_kcond((u32)nfds, sets[i], &kcond_count, gcf[i])))
         return rc;
   }

   printk("kcond count: %u\n", kcond_count);

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

      printk("[select (tid: %d)]: going to sleep on waiter obj\n", curr->tid);
      kernel_sleep_on_waiter(waiter);

      for (u32 j = 0; j < waiter->count; j++) {

         mwobj_elem *me = &waiter->elems[j];

         if (me->type && !me->wobj.type) {
            printk("[select]    -> condition #%u was signaled\n", j);
            mobj_waiter_reset(me);
         }
      }
   }

   free_mobj_waiter(waiter);
   return 0;
}
