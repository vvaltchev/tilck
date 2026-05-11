/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Layout globals + terminal-mode lifecycle for userspace TUIs.
 *
 * dp_init_layout measures the terminal via TIOCGWINSZ and populates
 * the globals so callers can paint a centered 76x23 panel area.
 * dp_term_setup switches the terminal into raw mode + alt buffer +
 * hidden cursor; dp_term_restore reverses all three.
 *
 * No panel state lives here — the dp panel context (dp_default_ctx /
 * dp_ctx) is in userapps/dp/dp_panel.c, since it exists for the
 * buffered-emit layer that doesn't ship in tools like the tracer.
 */

#include <stdbool.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "term.h"
#include "tui_input.h"
#include "tui_layout.h"

int dp_rows;
int dp_cols;
int dp_start_row;
int dp_end_row;
int dp_start_col;
int dp_screen_start_row;
int dp_screen_rows;

void dp_init_layout(void)
{
   struct winsize ws = {0};

   if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row && ws.ws_col) {
      dp_rows = ws.ws_row;
      dp_cols = ws.ws_col;
   } else {
      /* Fallback if the TTY won't tell us its size */
      dp_rows = 25;
      dp_cols = 80;
   }

   /*
    * The panel is centered horizontally and roughly vertically, with
    * the same margin layout the kernel-side dp_enter() uses.
    */
   dp_start_row = (dp_rows - DP_H) / 2 + 1;
   dp_start_col = (dp_cols - DP_W) / 2 + 1;
   dp_end_row   = dp_start_row + DP_H;
   dp_screen_start_row = dp_start_row + 3;
   dp_screen_rows = (DP_H - 2 - (dp_screen_start_row - dp_start_row));
}

static struct termios saved_termios;
static bool termios_saved;

void dp_term_setup(void)
{
   struct termios t;

   if (tcgetattr(STDIN_FILENO, &saved_termios) == 0) {

      termios_saved = true;
      t = saved_termios;

      /* Raw mode: no canonical input, no echo, no signal generation,
       * pass through control chars. Mirrors the kernel-side
       * tty_set_raw_mode behavior the in-kernel dp relied on. */
      t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
      t.c_oflag &= ~(OPOST);
      t.c_cflag |= (CS8);
      t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

      tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
   }

   dp_set_input_blocking(false);
   dp_set_cursor_enabled(false);
   dp_switch_to_alt_buffer();
}

void dp_term_restore(void)
{
   dp_switch_to_default_buffer();
   dp_set_cursor_enabled(true);
   dp_set_input_blocking(true);

   if (termios_saved) {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
      termios_saved = false;
   }
}
