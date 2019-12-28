/* SPDX-License-Identifier: BSD-2-Clause */

#include "tracing_int.h"

#include <sys/syscall.h> // system header

static const struct syscall_info __tracing_metadata[] =
{
   {
      .sys_n = SYS_read,
      .n_params = 3,
      .ret_type = sys_ret_type_val_or_errno,
      .pfmt = sys_fmt1,
      .params = {

         {
            .name = "fd",
            .type = &ptype_int,
            .kind = sys_param_in,
            .slot = NO_SLOT,
         },

         {
            .name = "buf",
            .type = &ptype_buffer,
            .kind = sys_param_out,
            .slot = 1,
            .size_param_name = "count",
            .real_sz_in_ret = true,
         },

         {
            .name = "count",
            .type = &ptype_int,
            .kind = sys_param_in,
            .slot = NO_SLOT,
         },
      },
   },

   {
      .sys_n = INVALID_SYSCALL,
   },
};

const struct syscall_info *tracing_metadata = __tracing_metadata;
