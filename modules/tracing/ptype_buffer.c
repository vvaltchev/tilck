/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/user.h>
#include <tilck/mods/tracing.h>

static bool
save_param_buffer(void *data, sptr data_sz, char *dest_buf, size_t __dest_bs)
{
   const sptr dest_bs = (sptr) __dest_bs;

   if (data_sz == -1) {
      /* assume that `data` is a C string */
      data_sz = (sptr)strlen(data) + 1;
   }

   const sptr actual_sz = MIN(data_sz, dest_bs);

   if (copy_from_user(dest_buf, data, (size_t)actual_sz)) {
      memcpy(dest_buf, "<fault>", 8);
   }

   return true;
}

static bool
dump_param_buffer(uptr orig,
                  char *data,
                  sptr data_bs,
                  sptr real_sz,
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
               snprintk(minibuf, sizeof(minibuf), "\\x%02x", (u8)c);
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

const struct sys_param_type ptype_buffer = {

   .name = "char *",
   .slot_size = 32,

   .save = save_param_buffer,
   .dump = dump_param_buffer,
   .dump_from_val = NULL,
};

const struct sys_param_type ptype_path = {

   .name = "char *",
   .slot_size = 64,

   .save = save_param_buffer,
   .dump = dump_param_buffer,
   .dump_from_val = NULL,
};
