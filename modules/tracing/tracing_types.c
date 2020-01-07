/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/user.h>
#include <tilck/kernel/sys_types.h>
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
   const int rc = (val != 0)
      ? snprintk(dest, dest_buf_size, "%p", val)
      : snprintk(dest, dest_buf_size, "NULL");

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

static bool
dump_param_errno_or_ptr(uptr __val, char *dest, size_t dest_buf_size)
{
   sptr val = (sptr)__val;
   int rc;

   rc = (val >= 0 || val < -500 /* the smallest errno */)
      ? snprintk(dest, dest_buf_size, "%p", val)
      : snprintk(dest, dest_buf_size, "-%s", get_errno_name((int)-val));

   return rc < (int)dest_buf_size;
}

bool
buf_append(char *dest, int *used, int *rem, char *str)
{
   int rc;
   ASSERT(*rem >= 0);

   if (*rem == 0)
      return false;

   rc = snprintk(dest + *used, (size_t) *rem, "%s", str);

   if (rc >= *rem)
      return false;

   *used += rc;
   *rem -= rc;
   return true;
}

static ALWAYS_INLINE bool
is_flag_on(uptr var, uptr fl)
{
   return (var & fl) == fl;
}

#define OPEN_CHECK_FLAG(x)                                           \
   if (is_flag_on(fl, x))                                            \
      if (!buf_append(dest, &used, &rem, #x "|"))                    \
         return false;

static bool
dump_param_open_flags(uptr fl, char *dest, size_t dest_buf_size)
{
   int rem = (int) dest_buf_size;
   int used = 0;

   if (fl == 0) {
      memcpy(dest, "0", 2);
      return true;
   }

   OPEN_CHECK_FLAG(O_APPEND)
   OPEN_CHECK_FLAG(O_ASYNC)
   OPEN_CHECK_FLAG(O_CLOEXEC)
   OPEN_CHECK_FLAG(O_CREAT)
   OPEN_CHECK_FLAG(O_DIRECT)
   OPEN_CHECK_FLAG(O_DIRECTORY)
   OPEN_CHECK_FLAG(O_DSYNC)
   OPEN_CHECK_FLAG(O_EXCL)
   OPEN_CHECK_FLAG(O_LARGEFILE)
   OPEN_CHECK_FLAG(O_NOATIME)
   OPEN_CHECK_FLAG(O_NOCTTY)
   OPEN_CHECK_FLAG(O_NOFOLLOW)
   OPEN_CHECK_FLAG(O_NONBLOCK)
   OPEN_CHECK_FLAG(O_NDELAY)
   OPEN_CHECK_FLAG(O_PATH)
   OPEN_CHECK_FLAG(O_SYNC)
   OPEN_CHECK_FLAG(O_TMPFILE)
   OPEN_CHECK_FLAG(O_TRUNC)

   ASSERT(dest[used - 1] == '|');
   dest[used - 1] = 0;
   return true;
}

const struct sys_param_type ptype_int = {

   .name = "int",
   .slot_size = 0,

   .save = NULL,
   .dump = NULL,
   .dump_from_val = dump_param_int,
};

const struct sys_param_type ptype_voidp = {

   .name = "void *",
   .slot_size = 0,

   .save = NULL,
   .dump = NULL,
   .dump_from_val = dump_param_voidp,
};

const struct sys_param_type ptype_oct = {

   .name = "oct",
   .slot_size = 0,

   .save = NULL,
   .dump = NULL,
   .dump_from_val = dump_param_oct,
};

const struct sys_param_type ptype_errno_or_val = {

   .name = "errno_or_val",
   .slot_size = 0,

   .save = NULL,
   .dump = NULL,
   .dump_from_val = dump_param_errno_or_val,
};

const struct sys_param_type ptype_errno_or_ptr = {

   .name = "errno_or_ptr",
   .slot_size = 0,

   .save = NULL,
   .dump = NULL,
   .dump_from_val = dump_param_errno_or_ptr,
};

const struct sys_param_type ptype_open_flags = {

   .name = "int",
   .slot_size = 0,

   .save = NULL,
   .dump = NULL,
   .dump_from_val = dump_param_open_flags,
};

struct saved_int_pair_data {

   bool valid;
   int pair[2];
};

static bool
save_param_int_pair(void *data, sptr unused, char *dest_buf, size_t dest_bs)
{
   struct saved_int_pair_data *saved_data = (void *)dest_buf;
   ASSERT(dest_bs >= sizeof(struct saved_int_pair_data));

   if (copy_from_user(saved_data->pair, data, sizeof(int) * 2))
      saved_data->valid = false;
   else
      saved_data->valid = true;

   return true;
}

static bool
dump_param_int_pair(uptr orig,
                    char *__data,
                    sptr unused1,
                    sptr unused2,
                    char *dest,
                    size_t dest_bs)
{
   int rc;
   struct saved_int_pair_data *data = (void *)__data;

   if (!data->valid) {
      snprintk(dest, dest_bs, "<fault>");
      return true;
   }

   rc = snprintk(dest, dest_bs, "{%d, %d}", data->pair[0], data->pair[1]);
   return rc <= (int) dest_bs;
}

const struct sys_param_type ptype_int32_pair = {

   .name = "int[2]",
   .slot_size = 16,

   .save = save_param_int_pair,
   .dump = dump_param_int_pair,
   .dump_from_val = NULL,
};
