/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Userspace counterpart of modules/debugpanel/dp_input.c. The ESC
 * sequence parser and the line-editor are unchanged in spirit; the
 * only differences are:
 *
 *   - Reads from STDIN_FILENO instead of a kernel fs_handle. The
 *     non-blocking + EAGAIN + sleep dance is preserved (the parser
 *     uses the EAGAIN path to detect a bare ESC keypress).
 *
 *   - Sleep replaced with usleep(40_000) (~25 Hz, matching the kernel's
 *     KRN_TIMER_HZ / 25 cadence).
 *
 *   - Multi-byte key constants are dp-private DP_KEY_*; no scancodes.
 *
 *   - dp_set_input_blocking() flips the O_NONBLOCK flag on stdin via
 *     fcntl().
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "termutil.h"
#include "dp_int.h"

static int line_pos;
static int line_len;
static char line[72];

typedef void (*key_handler_type)(char *, int);

void dp_set_input_blocking(bool blocking)
{
   int fl = fcntl(STDIN_FILENO, F_GETFL, 0);

   if (fl < 0)
      return;

   if (blocking)
      fl &= ~O_NONBLOCK;
   else
      fl |= O_NONBLOCK;

   (void)fcntl(STDIN_FILENO, F_SETFL, fl);
}

static int
read_single_byte(char *buf, int len)
{
   bool esc_timeout = false;
   ssize_t rc;
   char c;

   while (1) {

      rc = read(STDIN_FILENO, &c, 1);

      if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {

         if (len > 0 && buf[0] == DP_KEY_ESC) {

            /*
             * We hit a non-terminated escape sequence: wait for one
             * timeout interval and then return 0 if we hit EAGAIN
             * another time.
             */

            if (esc_timeout)
               return 0; /* stop reading */

            esc_timeout = true;
         }

         usleep(40 * 1000); /* ~25 Hz, mirrors KRN_TIMER_HZ / 25 */
         continue;
      }

      if (rc == 0)
         return 0;          /* stop reading (stdin closed) */

      if (rc < 0)
         return (int)-errno; /* propagate the error */

      break;
   }

   buf[len] = c;
   return 1; /* continue reading */
}

static void
convert_seq_to_key(char *buf, struct key_event *ke)
{
   /* ESC [ <n> ~ */
   static const unsigned helper_keys[6] = {
      DP_KEY_HOME, DP_KEY_INS, DP_KEY_DEL,
      DP_KEY_END, DP_KEY_PAGE_UP, DP_KEY_PAGE_DOWN,
   };

   if ((buf[0] >= 32 && buf[0] <= 127) || (buf[0] >= 1 && buf[0] <= 26)) {

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

      unsigned key = 0;

      switch (buf[2]) {

         case 'A':
            key = DP_KEY_UP;
            break;

         case 'B':
            key = DP_KEY_DOWN;
            break;

         case 'C':
            key = DP_KEY_RIGHT;
            break;

         case 'D':
            key = DP_KEY_LEFT;
            break;

         case '1':
         case '2':
         case '3':
         case '4':
         case '5':
         case '6':

            if (buf[3] == '~' && buf[2] >= '1' && buf[2] <= '6')
               key = helper_keys[buf[2] - '1'];

            break;

         /* Compatibility keys, for TERM != linux */
         case 'H':
            key = DP_KEY_HOME;
            break;

         case 'F':
            key = DP_KEY_END;
            break;
      }

      *ke = (struct key_event) {
         .pressed = true,
         .print_char = 0,
         .key = key,
      };
   }
   /* else: unknown ESC sequence — leave `ke` zeroed */
}

int
dp_read_ke_from_tty(struct key_event *ke)
{
   char c, buf[16];
   int rc;
   int len;

   enum {

      state_default,
      state_in_esc1,
      state_in_csi_param,
      state_in_csi_intermediate,

   } state = state_default;

   memset(ke, 0, sizeof(*ke));
   memset(buf, 0, sizeof(buf));

   for (len = 0; len < (int)sizeof(buf); len++) {

      rc = read_single_byte(buf, len);

      if (rc < 0 || (!rc && !len))
         return rc;

      if (!rc)
         break;

      c = buf[len];

   state_changed:

      switch (state) {

         case state_in_csi_intermediate:

            if (c >= 0x20 && c <= 0x2F)
               continue; /* for loop */

            /*
             * The current char must be in range 0x40-0x7E, but we must
             * break anyway, even if it isn't.
             */

            break; /* switch (state) */

         case state_in_csi_param:

            if (c >= 0x30 && c <= 0x3F)
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
   (void)buf; (void)bs;
   dp_move_left(line_pos);
   line_pos = 0;
}

static void
handle_seq_end(char *buf, int bs)
{
   (void)buf; (void)bs;
   dp_move_right(line_len - line_pos);
   line_pos = line_len;
}

static void
handle_seq_delete(char *buf, int bs)
{
   (void)bs;

   if (!line_len || line_pos == line_len)
      return;

   line_len--;

   for (int i = line_pos; i < line_len + 1; i++)
      buf[i] = buf[i + 1];

   buf[line_len] = ' ';
   dp_write_raw_int(buf + line_pos, line_len - line_pos + 1);
   dp_move_left(line_len - line_pos + 1);
}

static void
handle_seq_left(char *buf, int bs)
{
   (void)buf; (void)bs;

   if (!line_pos)
      return;

   dp_move_left(1);
   line_pos--;
}

static void
handle_seq_right(char *buf, int bs)
{
   (void)buf; (void)bs;

   if (line_pos >= line_len)
      return;

   dp_move_right(1);
   line_pos++;
}

static void
handle_esc_seq(unsigned key, char *buf, int buf_size)
{
   key_handler_type func = NULL;

   switch (key) {

      case DP_KEY_LEFT:
         func = handle_seq_left;
         break;

      case DP_KEY_RIGHT:
         func = handle_seq_right;
         break;

      case DP_KEY_HOME:
         func = handle_seq_home;
         break;

      case DP_KEY_END:
         func = handle_seq_end;
         break;

      case DP_KEY_DEL:
         func = handle_seq_delete;
         break;
   }

   if (func)
      func(buf, buf_size);
}

static void
handle_backspace(char *buf, int buf_size)
{
   (void)buf_size;

   if (!line_len || !line_pos)
      return;

   line_len--;
   line_pos--;
   dp_erase_last();

   if (line_pos == line_len)
      return;

   /* Shift left all chars after line_pos */
   for (int i = line_pos; i < line_len + 1; i++)
      buf[i] = buf[i+1];

   buf[line_len] = ' ';
   dp_write_raw_int(buf + line_pos, line_len - line_pos + 1);
   dp_move_left(line_len - line_pos + 1);
}

static bool
handle_regular_char(char c, char *buf, int bs)
{
   (void)bs;

   dp_write_raw_int(&c, 1);

   if (c == '\r' || c == '\n')
      return false;

   if (line_pos == line_len) {

      buf[line_pos++] = c;

   } else {

      /* Shift right all chars after line_pos */
      for (int i = line_len; i >= line_pos; i--)
         buf[i + 1] = buf[i];

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
   const int max_line_len =
      buf_size - 1 < (int)sizeof(line) - 1
      ? buf_size - 1
      : (int)sizeof(line) - 1;

   line_len = (int)strlen(buf);
   line_pos = line_len;

   memcpy(line, buf, (size_t)max_line_len);
   line[line_len] = 0;

   dp_write_raw("%s", line);

   while (1) {

      rc = dp_read_ke_from_tty(&ke);

      if (rc < 0) {
         line_len = rc;
         break;
      }

      c = ke.print_char;

      if (line_len < max_line_len) {

         if (c == DP_KEY_BACKSPACE || c == '\b') {

            handle_backspace(buf, buf_size);

         } else if (!c && ke.key) {

            handle_esc_seq(ke.key, buf, buf_size);

         } else if (isprint((unsigned char)c) || c == '\r' || c == '\n') {

            if (!handle_regular_char(c, buf, buf_size))
               break;
         }

      } else {

         /* line_len == max_line_len */

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
