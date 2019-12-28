/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>
#include <tilck/kernel/user.h>

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

static bool
save_param_buffer(void *data, sptr data_sz, char *dest_buf, size_t __dest_bs)
{
   ASSERT(data_sz > 0);
   const sptr dest_bs = (sptr) __dest_bs;
   const sptr actual_sz = MIN(data_sz, dest_bs);

   if (copy_from_user(dest_buf, data, (size_t)actual_sz)) {
      memcpy(dest_buf, "<fault>", 8);
   }

   return true;
}

static bool
write_to_buf(char *buf, char *buf_end, char c)
{
   if (buf == buf_end)
      return false;

   *buf = c;
   return true;
}

static bool
dump_param_buffer(char *data,
                  sptr data_bs,
                  sptr real_sz,
                  char *dest,
                  size_t dest_bs)
{
   ASSERT(data_bs > 0);
   char minibuf[8];
   char *s;
   char *data_end = data + (real_sz < 0 ? data_bs : MIN(real_sz, data_bs));
   char *dest_end = dest + dest_bs;

   write_to_buf(dest++, dest_end, '\"');

   for (s = data; s < data_end; s++) {

      char c = *s;
      sptr ml = 0;

      if (isprint(c)) {

         if (!write_to_buf(dest++, dest_end, c))
            break;

         continue;
      }

      if (c == '\n') {

         snprintk(minibuf, sizeof(minibuf), "\\n");

      } else if (c== '\r') {

         snprintk(minibuf, sizeof(minibuf), "\\r");

      } else if (c == '\"') {

         snprintk(minibuf, sizeof(minibuf), "\\\"");

      } else {

         snprintk(minibuf, sizeof(minibuf), "\\x%02x", (u32)c);
      }

      ml = (sptr)strlen(minibuf);

      if (dest_end - dest < ml - 1) {
         dest = dest_end;
         break;
      }

      memcpy(dest, minibuf, (size_t)ml);
      dest += ml;
   }

   if (dest >= dest_end - 4) {

      dest[-1] = 0;
      dest[-2] = '\"';
      dest[-3] = '.';
      dest[-4] = '.';
      dest[-5] = '.';

   } else {

      if (s == data_end && real_sz > 0 && data_bs < real_sz) {
         *dest++ = '.';
         *dest++ = '.';
         *dest++ = '.';
      }

      *dest++ = '\"';
      *dest = 0;
   }

   return true;
}

const struct sys_param_type ptype_int = {

   .name = "int",
   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = dump_param_int,
};

const struct sys_param_type ptype_voidp = {

   .name = "void *",
   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = dump_param_voidp,
};

const struct sys_param_type ptype_buffer = {

   .name = "char *",
   .save = save_param_buffer,
   .dump_from_data = dump_param_buffer,
   .dump_from_val = NULL,
};
