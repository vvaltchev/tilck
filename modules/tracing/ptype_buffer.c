/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The buffer-pointer ptypes (ptype_buffer / ptype_big_buf /
 * ptype_path) along with the save callback that copy_from_user's
 * the buffer contents into the trace_event's saved-params slot.
 *
 * The matching dump callback that escaped saved bytes back into a
 * "..." literal moved to userspace dp (userapps/dp/tr_dump.c) when
 * trace-event rendering was extracted from the kernel.
 */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/user.h>
#include <tilck/mods/tracing.h>

STATIC bool
save_param_buffer(void *data, long data_sz, char *dest_buf, size_t dest_bs)
{
   if (data_sz == -1) {

      /* Assume `data` is a C string. */
      int rc = copy_str_from_user(dest_buf, data, dest_bs, NULL);

      if (rc < 0) {

         /* Reading from `data` caused a PAGE fault. */
         memcpy(dest_buf, "<fault>", 8);

      } else if (rc > 0) {

         /* The user buffer is bigger than our reserved space:
          * truncate it. */
         dest_buf[dest_bs - 1] = 0;
      }

   } else {

      ASSERT(data_sz >= 0);
      const size_t actual_sz = MIN((size_t)data_sz, dest_bs);

      if (copy_from_user(dest_buf, data, actual_sz))
         memcpy(dest_buf, "<fault>", 8);
   }

   return true;
}

const struct sys_param_type ptype_buffer = {
   .name      = "char *",
   .slot_size = 32,
   .ui_type   = ui_type_string,
   .save      = save_param_buffer,
};

const struct sys_param_type ptype_big_buf = {
   .name      = "char *",
   .slot_size = 128,
   .ui_type   = ui_type_string,
   .save      = save_param_buffer,
};

const struct sys_param_type ptype_path = {
   .name      = "char *",
   .slot_size = 64,
   .ui_type   = ui_type_string,
   .save      = save_param_buffer,
};
