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

      printk("    %s = [ ", name);

      for (int i = 0; i < nfds; i++)
         if (FD_ISSET(i, s))
            printk(NO_PREFIX "%d ", i);

      printk(NO_PREFIX "]\n");

   } else {
      printk("    %s = NULL,\n", name);
   }
}

static void
debug_dump_select_args(int nfds, fd_set *rfds, fd_set *wfds,
                       fd_set *efds, struct timeval *tout)
{
   printk("sys_select(\n");
   printk("    nfds = %d,\n", nfds);

   debug_dump_fds("rfds", nfds, rfds);
   debug_dump_fds("wfds", nfds, wfds);
   debug_dump_fds("efds", nfds, efds);

   printk("    tout: %u secs, %u usecs\n", tout->tv_sec,tout->tv_usec);

   printk(")\n");
}

sptr sys_select(int nfds, fd_set *user_rfds, fd_set *user_wfds,
                fd_set *user_efds, struct timeval *user_tout)
{
   task_info *curr = get_curr_task();
   fd_set *rfds, *wfds, *efds;
   struct timeval *tout;

   rfds = user_rfds ? ((fd_set*) curr->args_copybuf) + 0 : NULL;
   wfds = user_wfds ? ((fd_set*) curr->args_copybuf) + 1 : NULL;
   efds = user_efds ? ((fd_set*) curr->args_copybuf) + 2 : NULL;
   tout = user_tout ? (void *) ((fd_set *) curr->args_copybuf) + 3 : NULL;

   if (rfds && copy_from_user(rfds, user_rfds, sizeof(fd_set)))
      return -EBADF;
   if (wfds && copy_from_user(wfds, user_wfds, sizeof(fd_set)))
      return -EBADF;
   if (efds && copy_from_user(efds, user_efds, sizeof(fd_set)))
      return -EBADF;
   if (tout && copy_from_user(tout, user_tout, sizeof(struct timeval)))
      return -EBADF;

   debug_dump_select_args(nfds, rfds, wfds, efds, tout);

   return -ENOSYS;
}
