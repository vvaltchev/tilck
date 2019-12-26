/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/sched.h>

#include "termutil.h"
#include "dp_int.h"

static int
read_single_byte(fs_handle h, char *buf, u32 len)
{
   bool esc_timeout = false;
   int rc;
   char c;

   while (true) {

      rc = vfs_read(h, &c, 1);

      if (rc == -EAGAIN) {

         if (len > 0 && buf[0] == DP_KEY_ESC) {

            /*
             * We hit a non-terminated escape sequence: let's wait for one
             * timeout interval and then return 0 if we hit EAGAIN another time.
             */

            if (esc_timeout)
               return 0; /* stop reading */

            esc_timeout = true;
         }

         kernel_sleep(TIMER_HZ / 25);
         continue;
      }

      if (rc == 0)
         return 0; /* stop reading */

      if (rc < 0)
         return rc; /* error */

      break;
   }

   buf[len] = c;
   return 1; /* continue reading */
}

static void
convert_seq_to_key(char *buf, struct key_event *ke)
{
   if (IN_RANGE_INC(buf[0], 32, 127) || IN_RANGE_INC(buf[0], 1, 26)) {

      *ke = (struct key_event) {
         .pressed = true,
         .print_char = buf[0],
         .key = 0,
      };

   } else if (buf[0] == DP_KEY_ESC && !buf[1]) {

      *ke = (struct key_event) {
         .pressed = true,
         .print_char = buf[0],
         .key = 0,
      };

   } else if (buf[0] == DP_KEY_ESC && buf[1] == '[') {

      u32 key = 0;

      switch (buf[2]) {

         case 'A':
            key = KEY_UP;
            break;

         case 'B':
            key = KEY_DOWN;
            break;

         case 'C':
            key = KEY_RIGHT;
            break;

         case 'D':
            key = KEY_LEFT;
            break;

         case '5':
         case '6':

            if (buf[3] == '~')
               key = buf[2] == '5' ? KEY_PAGE_UP : KEY_PAGE_DOWN;

            break;
      }

      *ke = (struct key_event) {
         .pressed = true,
         .print_char = 0,
         .key = key,
      };

   } else {

      /* Unknown ESC sequence: do nothing (`ke` will remain zeroed) */
   }
}

int
dp_read_ke_from_tty(struct key_event *ke)
{
   fs_handle h = dp_input_handle;
   char c, buf[16];
   int rc;
   u32 len;

   enum {

      state_default,
      state_in_esc1,
      state_in_csi_param,
      state_in_csi_intermediate,

   } state = state_default;

   bzero(ke, sizeof(*ke));
   bzero(buf, sizeof(buf));

   for (len = 0; len < sizeof(buf); len++) {

      rc = read_single_byte(h, buf, len);

      if (rc < 0 || (!rc && !len))
         return rc;

      if (!rc)
         break;

      c = buf[len];

   state_changed:

      switch (state) {

         case state_in_csi_intermediate:

            if (IN_RANGE_INC(c, 0x20, 0x2F))
               continue; /* for loop */

            /*
             * The current char must be in range 0x40-0x7E, but we must break
             * anyway, even it isn't.
             */

            break; /* switch (state) */

         case state_in_csi_param:

            if (IN_RANGE_INC(c, 0x30, 0x3F))
               continue; /* for loop */

            state = state_in_csi_intermediate;
            goto state_changed;

         case state_in_esc1:

            if (c == '[') {
               state = state_in_csi_param;
               continue; /* for loop */
            }

            /* any other non-CSI sequence is ignored */
            break; /* switch (state) */

         case state_default:

            if (c == 27) {
               state = state_in_esc1;
               continue; /* for loop */
            }

            break; /* switch (state) */

         default:
            NOT_REACHED();
      }

      break; /* for (len = 0; len < sizeof(buf); len++) */
   }

   convert_seq_to_key(buf, ke);
   return 0;
}
