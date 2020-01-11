/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/sched.h>

#include "termutil.h"
#include "dp_int.h"

static int line_pos;
static int line_len;
static char line[72];

typedef void (*key_handler_type)(char *, int);


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

         case 'H':
            key = KEY_HOME;
            break;

         case 'F':
            key = KEY_END;
            break;

         case '3':

            if (buf[3] == '~')
               key = KEY_DEL;

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

static inline void dp_erase_last(void)
{
   dp_write_raw("\033[D \033[D");
}

static void
handle_seq_home(char *buf, int bs)
{
   dp_move_left(line_pos);
   line_pos = 0;
}

static void
handle_seq_end(char *buf, int bs)
{
   dp_move_right(line_len - line_pos);
   line_pos = line_len;
}

static void
handle_seq_delete(char *buf, int bs)
{
   if (!line_len || line_pos == line_len)
      return;

   line_len--;

   for (int i = line_pos; i < line_len + 1; i++) {
      buf[i] = buf[i + 1];
   }

   buf[line_len] = ' ';
   dp_write_raw_int(buf + line_pos, line_len - line_pos + 1);
   dp_move_left(line_len - line_pos + 1);
}

static void
handle_seq_left(char *buf, int bs)
{
   if (!line_pos)
      return;

   dp_move_left(1);
   line_pos--;
}

static void
handle_seq_right(char *buf, int bs)
{
   if (line_pos >= line_len)
      return;

   dp_move_right(1);
   line_pos++;
}

static void
handle_esc_seq(u32 key, char *buf, int buf_size)
{
   key_handler_type func = NULL;

   switch (key) {

      case KEY_LEFT:
         func = handle_seq_left;
         break;

      case KEY_RIGHT:
         func = handle_seq_right;
         break;

      case KEY_HOME:
         func = handle_seq_home;
         break;

      case KEY_END:
         func = handle_seq_end;
         break;

      case KEY_DEL:
         func = handle_seq_delete;
         break;
   }

   if (func)
      func(buf, buf_size);
}

static void
handle_backspace(char *buf, int buf_size)
{
   if (!line_len || !line_pos)
      return;

   line_len--;
   line_pos--;
   dp_erase_last();

   if (line_pos == line_len)
      return;

   /* We have to shift left all the chars after line_pos */
   for (int i = line_pos; i < line_len + 1; i++) {
      buf[i] = buf[i+1];
   }

   buf[line_len] = ' ';
   dp_write_raw_int(buf + line_pos, line_len - line_pos + 1);
   dp_move_left(line_len - line_pos + 1);
}

static bool
handle_regular_char(char c, char *buf, int bs)
{
   dp_write_raw_int(&c, 1);

   if (c == '\r' || c == '\n')
      return false;

   if (line_pos == line_len) {

      buf[line_pos++] = c;

   } else {

      /* We have to shift right all the chars after line_pos */
      for (int i = line_len; i >= line_pos; i--) {
         buf[i + 1] = buf[i];
      }

      buf[line_pos] = c;

      dp_write_raw_int(buf + line_pos + 1, line_len - line_pos);
      line_pos++;

      dp_move_left(line_len - line_pos + 1);
   }

   line_len++;
   return true;
}

int dp_read_line(char *buf, int buf_size)
{
   int rc;
   char c;
   struct key_event ke;
   const int max_line_len = MIN(buf_size - 1, (int)sizeof(line) - 1);

   line_len = line_pos = 0;

   line_len = (int)strlen(buf);
   line_pos = line_len;

   memcpy(line, buf, (size_t)max_line_len);
   line[line_len] = 0;

   dp_write_raw("%s", line);

   while (true) {

      rc = dp_read_ke_from_tty(&ke);

      if (rc < 0) {
         line_len = rc;
         break;
      }

      c = ke.print_char;

      if (line_len < max_line_len) {

         if (c == DP_KEY_BACKSPACE) {

            handle_backspace(buf, buf_size);

         } else if (!c && ke.key) {

            handle_esc_seq(ke.key, buf, buf_size);

         } else if (isprint(c) || c == '\r' || c == '\n') {

            if (!handle_regular_char(c, buf, buf_size))
               break;
         }

      } else {

         ASSERT(line_len == max_line_len);

         if (c == DP_KEY_BACKSPACE) {

            handle_backspace(buf, buf_size);

         } else if (c == '\r' || c == '\n') {

            dp_write_raw("\r\n");
            break;
         }
      }
   }

   buf[line_len >= 0 ? line_len : 0] = 0;
   return line_len;
}
