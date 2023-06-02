/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/user.h>
#include <tilck/mods/tracing.h>

STATIC bool
save_param_buffer(void *data, long data_sz, char *dest_buf, size_t dest_bs)
{
   if (data_sz == -1) {

      /* assume that `data` is a C string */
      int rc = copy_str_from_user(dest_buf, data, dest_bs, NULL);

      if (rc < 0) {

         /* Reading from `data` caused a PAGE fault */
         memcpy(dest_buf, "<fault>", 8);

      } else if (rc > 0) {

         /*
          * The user buffer is bigger than our reserved space: just truncate it.
          */
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

STATIC bool
dump_param_buffer(ulong orig,
                  char *data,
                  long data_bs,
                  long real_sz,
                  char *dest,
                  size_t dest_bs)
{
   ASSERT(dest_bs > 8);

   if (!orig) {
      snprintk(dest, dest_bs, "NULL");
      return true;
   }

   if (data_bs == -1) {
      /* assume that `data` is a C string */
      data_bs = (long)strlen(data);
   }

   if (!tracing_are_dump_big_bufs_on() && real_sz > 0)
      real_sz = MIN(real_sz, 16);

   char minibuf[8];
   char *s;
   char *data_end = data + (real_sz < 0 ? data_bs : MIN(real_sz, data_bs));
   char *dest_end = dest + dest_bs;

   *dest++ = '\"';

   for (s = data; s < data_end; s++) {

      char c = *s;
      long ml = 0;

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
               snprintk(minibuf, sizeof(minibuf), "\\x%x", (u8)c);
            }
      }

      ml = (long)strlen(minibuf);

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

const struct sys_param_type ptype_buffer = {

   .name = "char *",
   .slot_size = 32,
   .ui_type = ui_type_string,

   .save = save_param_buffer,
   .dump = dump_param_buffer,
   .dump_from_val = NULL,
};

const struct sys_param_type ptype_big_buf = {

   .name = "char *",
   .slot_size = 128,
   .ui_type = ui_type_string,

   .save = save_param_buffer,
   .dump = dump_param_buffer,
   .dump_from_val = NULL,
};

const struct sys_param_type ptype_path = {

   .name = "char *",
   .slot_size = 64,
   .ui_type = ui_type_string,

   .save = save_param_buffer,
   .dump = dump_param_buffer,
   .dump_from_val = NULL,
};
