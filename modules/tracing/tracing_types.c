/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>
#include <tilck/kernel/user.h>
#include <tilck/mods/tracing.h>

static bool
dump_param_int(uptr __val, char *dest, size_t dest_buf_size)
{
   const sptr val = (sptr)__val;
   int rc;

   rc = snprintk(dest,
                 dest_buf_size,
                 NBITS == 32 ? "%d" : "%lld",
                 val);

   return rc < (int)dest_buf_size;
}

static bool
dump_param_voidp(uptr val, char *dest, size_t dest_buf_size)
{
   int rc = snprintk(dest, dest_buf_size, "%p", val);
   return rc < (int)dest_buf_size;
}

static bool
dump_param_oct(uptr __val, char *dest, size_t dest_buf_size)
{
   int val = (int)__val;
   int rc;

   rc = snprintk(dest, dest_buf_size, "0%03o", val);
   return rc < (int)dest_buf_size;
}

static bool
dump_param_errno_or_val(uptr __val, char *dest, size_t dest_buf_size)
{
   int val = (int)__val;
   int rc;

   rc = (val >= 0)
      ? snprintk(dest, dest_buf_size, "%d", val)
      : snprintk(dest, dest_buf_size, "-%s", get_errno_name(-val));

   return rc < (int)dest_buf_size;
}

const struct sys_param_type ptype_int = {

   .name = "int",
   .slot_size = 0,

   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = dump_param_int,
};

const struct sys_param_type ptype_voidp = {

   .name = "void *",
   .slot_size = 0,

   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = dump_param_voidp,
};

const struct sys_param_type ptype_oct = {

   .name = "oct",
   .slot_size = 0,

   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = dump_param_oct,
};

const struct sys_param_type ptype_errno_or_val = {

   .name = "errno_or_val",
   .slot_size = 0,

   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = dump_param_errno_or_val,
};
