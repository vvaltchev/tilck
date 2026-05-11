/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Layout globals + terminal-mode lifecycle for userspace TUIs.
 *
 * tui_init_layout measures the terminal via TIOCGWINSZ and populates
 * the globals so callers can paint a centered 76x23 panel area.
 * tui_term_setup switches the terminal into raw mode + alt buffer +
 * hidden cursor; tui_term_restore reverses all three.
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

int tui_rows;
int tui_cols;
int tui_start_row;
int tui_end_row;
int tui_start_col;
int tui_screen_start_row;
int tui_screen_rows;

void tui_init_layout(void)
{
   struct winsize ws = {0};

   if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row && ws.ws_col) {
      tui_rows = ws.ws_row;
      tui_cols = ws.ws_col;
   } else {
      /* Fallback if the TTY won't tell us its size */
      tui_rows = 25;
      tui_cols = 80;
   }

   /*
    * The panel is centered horizontally and roughly vertically, with
    * the same margin layout the kernel-side dp_enter() uses.
    */
   tui_start_row = (tui_rows - DP_H) / 2 + 1;
   tui_start_col = (tui_cols - DP_W) / 2 + 1;
   tui_end_row   = tui_start_row + DP_H;
   tui_screen_start_row = tui_start_row + 3;
   tui_screen_rows = (DP_H - 2 - (tui_screen_start_row - tui_start_row));
}

static struct termios saved_termios;
static bool termios_saved;

void tui_term_setup(void)
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

   tui_set_input_blocking(false);
   term_cursor_enable(false);
   term_alt_buffer_enter();
}

void tui_term_restore(void)
{
   term_alt_buffer_exit();
   term_cursor_enable(true);
   tui_set_input_blocking(true);

   if (termios_saved) {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
      termios_saved = false;
   }
}
