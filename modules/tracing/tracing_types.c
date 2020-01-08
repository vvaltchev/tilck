/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/user.h>
#include <tilck/kernel/sys_types.h>
#include <tilck/mods/tracing.h>

static bool
dump_param_int(ulong __val, long hlp, char *dest, size_t dest_buf_size)
{
   const long val = (long)__val;
   int rc;

   rc = snprintk(dest,
                 dest_buf_size,
                 NBITS == 32 ? "%d" : "%lld",
                 val);

   return rc < (int)dest_buf_size;
}

static bool
dump_param_voidp(ulong val, long hlp, char *dest, size_t dest_buf_size)
{
   const int rc = (val != 0)
      ? snprintk(dest, dest_buf_size, "%p", val)
      : snprintk(dest, dest_buf_size, "NULL");

   return rc < (int)dest_buf_size;
}

static bool
dump_param_oct(ulong __val, long hlp, char *dest, size_t dest_buf_size)
{
   int val = (int)__val;
   int rc;

   rc = snprintk(dest, dest_buf_size, "0%03o", val);
   return rc < (int)dest_buf_size;
}

static bool
dump_param_errno_or_val(ulong __val, long hlp, char *dest, size_t dest_buf_size)
{
   int val = (int)__val;
   int rc;

   rc = (val >= 0)
      ? snprintk(dest, dest_buf_size, "%d", val)
      : snprintk(dest, dest_buf_size, "-%s", get_errno_name(-val));

   return rc < (int)dest_buf_size;
}

static bool
dump_param_errno_or_ptr(ulong __val, long hlp, char *dest, size_t dest_buf_size)
{
   long val = (long)__val;
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
is_flag_on(ulong var, ulong fl)
{
   return (var & fl) == fl;
}

#define OPEN_CHECK_FLAG(x)                                           \
   if (is_flag_on(fl, x))                                            \
      if (!buf_append(dest, &used, &rem, #x "|"))                    \
         return false;

static bool
dump_param_open_flags(ulong fl, long hlp, char *dest, size_t dest_buf_size)
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

static bool
dump_param_doff64(ulong hi, long hlp, char *dest, size_t dest_bs)
{
   const ulong low = (ulong)hlp;
   const u64 val = ((u64)hi) << 32 | (u64)low;
   const int rc = snprintk(dest, dest_bs, "%llu", val);

   return rc < (int)dest_bs;
}

static bool
dump_param_whence(ulong val, long hlp, char *dest, size_t dest_bs)
{
   int rc;

   switch (val) {

      case SEEK_SET:
         rc = snprintk(dest, dest_bs, "SEEK_SET");
         break;

      case SEEK_CUR:
         rc = snprintk(dest, dest_bs, "SEEK_CUR");
         break;

      case SEEK_END:
         rc = snprintk(dest, dest_bs, "SEEK_END");
         break;

      default:
         rc = snprintk(dest, dest_bs, "unknown: %d", (int)val);
   }

   return rc < (int)dest_bs;
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

const struct sys_param_type ptype_doff64 = {

   .name = "ulong",
   .slot_size = 0,

   .save = NULL,
   .dump = NULL,
   .dump_from_val = dump_param_doff64,
};

const struct sys_param_type ptype_whence = {

   .name = "char *",
   .slot_size = 0,

   .save = NULL,
   .dump = NULL,
   .dump_from_val = dump_param_whence,
};

struct saved_int_pair_data {

   bool valid;
   int pair[2];
};

static bool
save_param_int_pair(void *data, long unused, char *dest_buf, size_t dest_bs)
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
dump_param_int_pair(ulong orig,
                    char *__data,
                    long unused1,
                    long unused2,
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
   .slot_size = 32,

   .save = save_param_int_pair,
   .dump = dump_param_int_pair,
   .dump_from_val = NULL,
};

static bool
save_param_u64_ptr(void *data, long unused, char *dest_buf, size_t dest_bs)
{
   ASSERT(dest_bs >= 8);
   u64 val;

   if (copy_from_user(&val, data, 8)) {
      snprintk(dest_buf, dest_bs, "<fault>");
      return true;
   }

   snprintk(dest_buf, dest_bs, "%llu", val);
   return true;
}

static bool
dump_param_u64_ptr(ulong orig,
                   char *data,
                   long unused1,
                   long unused2,
                   char *dest,
                   size_t dest_bs)
{
   memcpy(dest, data, strlen(data) + 1);
   return true;
}

const struct sys_param_type ptype_u64_ptr = {

   .name = "u64",
   .slot_size = 32,

   .save = save_param_u64_ptr,
   .dump = dump_param_u64_ptr,
   .dump_from_val = NULL,
};
