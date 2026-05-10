/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The struct iovec ptypes (ptype_iov_in / ptype_iov_out) along with
 * the save callback that copy_from_user's the iovec descriptors plus
 * the first few bytes of each iov_base into the trace_event's
 * saved-params slot.
 *
 * The matching dump callback that turned the captured bytes back
 * into a human-readable "(struct iovec[N]) {{base: ..., len: ...},
 * ...}" string moved to userspace dp (userapps/dp/tr_dump.c) when
 * trace-event rendering was extracted from the kernel.
 */

#include <tilck/common/printk.h>

#include <tilck/kernel/user.h>
#include <tilck/mods/tracing.h>

static bool
save_param_iov(void *data, long iovcnt, char *dest_buf, size_t dest_bs)
{
   struct iovec *u_iovec = data;
   struct iovec iovec[4];
   bool ok;

   ASSERT(dest_bs >= 128);

   if (iovcnt <= 0)
      return false;

   iovcnt = MIN(iovcnt, 4);

   if (copy_from_user(iovec, u_iovec, sizeof(iovec[0]) * (size_t)iovcnt))
      return false;

   for (int i = 0; i < iovcnt; i++) {

      ((long *)(void *)(dest_buf + 0))[i] = (long)iovec[i].iov_len;
      ((ulong *)(void *)(dest_buf + 32))[i] = (ulong)iovec[i].iov_base;

      ok = ptype_buffer.save(iovec[i].iov_base,
                             (long)iovec[i].iov_len,
                             dest_buf + 64 + 16 * i,
                             16);

      if (!ok)
         return false;
   }

   return false;
}

const struct sys_param_type ptype_iov_in = {
   .name      = "iov",
   .slot_size = 128,
   .save      = save_param_iov,
};

const struct sys_param_type ptype_iov_out = {
   .name      = "iov",
   .slot_size = 128,
   .save      = save_param_iov,
};
