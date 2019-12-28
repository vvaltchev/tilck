/* SPDX-License-Identifier: BSD-2-Clause */

#include "tracing_int.h"

#include <sys/syscall.h> // system header

#define SIMPLE_PARAM(_name, _type, _kind)                \
   {                                                     \
      .name = _name,                                     \
      .type = _type,                                     \
      .kind = _kind,                                     \
      .slot = NO_SLOT,                                   \
   }


static const struct syscall_info __tracing_metadata[] =
{
   {
      .sys_n = SYS_read,
      .n_params = 3,
      .ret_type = &ptype_int,
      .pfmt = sys_fmt1,
      .params = {

         SIMPLE_PARAM("fd", &ptype_int, sys_param_in),

         {
            .name = "buf",
            .type = &ptype_buffer,
            .kind = sys_param_out,
            .slot = 1,
            .size_param_name = "count",
            .real_sz_in_ret = true,
         },

         SIMPLE_PARAM("count", &ptype_int, sys_param_in),
      },
   },

   {
      .sys_n = INVALID_SYSCALL,
   },
};

const struct syscall_info *tracing_metadata = __tracing_metadata;
