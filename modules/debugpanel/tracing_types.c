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
dump_param_oct(uptr __val, char *dest, size_t dest_buf_size)
{
   int val = (int)__val;
   int rc;

   rc = snprintk(dest, dest_buf_size, "0%03o", val);
   return rc < (int)dest_buf_size;
}

static bool
save_param_buffer(void *data, sptr data_sz, char *dest_buf, size_t __dest_bs)
{
   if (data_sz == -1) {
      /* assume that `data` is a C string */
      data_sz = (sptr)strlen(data) + 1;
   }

   const sptr dest_bs = (sptr) __dest_bs;
   const sptr actual_sz = MIN(data_sz, dest_bs);

   if (copy_from_user(dest_buf, data, (size_t)actual_sz)) {
      memcpy(dest_buf, "<fault>", 8);
   }

   return true;
}

static bool
dump_param_buffer(char *data,
                  sptr data_bs,
                  sptr real_sz,
                  char *dest,
                  size_t dest_bs)
{
   ASSERT(dest_bs > 8);

   if (data_bs == -1) {
      /* assume that `data` is a C string */
      data_bs = (sptr)strlen(data);
   }

   char minibuf[8];
   char *s;
   char *data_end = data + (real_sz < 0 ? data_bs : MIN(real_sz, data_bs));
   char *dest_end = dest + dest_bs;

   *dest++ = '\"';

   for (s = data; s < data_end; s++) {

      char c = *s;
      sptr ml = 0;

      switch (c) {
         case '\n':
            snprintk(minibuf, sizeof(minibuf), "\\n");
            break;

         case '\r':
            snprintk(minibuf, sizeof(minibuf), "\\r");
            break;

         case '\"':
            snprintk(minibuf, sizeof(minibuf), "\\\"");
            break;

         case '\\':
            snprintk(minibuf, sizeof(minibuf), "\\\\");
            break;

         default:

            if (isprint(c)) {
               minibuf[0] = c;
               minibuf[1] = 0;
            } else {
               snprintk(minibuf, sizeof(minibuf), "\\x%02x", (u32)c);
            }
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

const struct sys_param_type ptype_buffer = {

   .name = "char *",
   .slot_size = 32,

   .save = save_param_buffer,
   .dump_from_data = dump_param_buffer,
   .dump_from_val = NULL,
};

const struct sys_param_type ptype_path = {

   .name = "char *",
   .slot_size = 64,

   .save = save_param_buffer,
   .dump_from_data = dump_param_buffer,
   .dump_from_val = NULL,
};
