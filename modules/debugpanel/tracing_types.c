/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>
#include "tracing_int.h"

static bool
dump_param_int(uptr __val, char *dest, size_t dest_buf_size)
{
   int val = (int)__val;
   int rc;

   rc = snprintk(dest, dest_buf_size, "%d", val);
   return rc < (int)dest_buf_size;
}

static bool
dump_param_voidp(uptr val, char *dest, size_t dest_buf_size)
{
   int rc = snprintk(dest, dest_buf_size, "%p", val);
   return rc < (int)dest_buf_size;
}

const struct sys_param_type ptype_int = {

   .name = "int",
   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = &dump_param_int,
};

const struct sys_param_type ptype_voidp = {

   .name = "voidp",
   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = &dump_param_voidp,
};
