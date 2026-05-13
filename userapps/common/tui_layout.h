/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Panel-area layout globals + terminal-mode lifecycle helpers.
 *
 * Any userspace TUI that wants to host a centered 76x23 panel within
 * the terminal calls tui_init_layout() once at startup to measure the
 * terminal and populate the globals below, then tui_term_setup() to
 * switch into raw-mode/alt-buffer and tui_term_restore() at exit.
 *
 * The DP_W / DP_H constants describe the standard inner panel area —
 * they're kept here (not in dp/) because the layout math that uses
 * them lives here, and any future TUI naturally inherits the same
 * default size by linking userapps/common/.
 */

#pragma once

#include <stdbool.h>

/* Inner panel area in characters (matches the kernel value). */
#define DP_W   76
#define DP_H   23

/* Layout state — initialized by tui_init_layout(). */
extern int tui_rows;             /* terminal rows */
extern int tui_cols;             /* terminal cols */
extern int tui_start_row;        /* panel top */
extern int tui_end_row;          /* panel bottom */
extern int tui_start_col;        /* panel left */
extern int tui_screen_start_row; /* first row of the content area */
extern int tui_screen_rows;      /* height of the content area */

/* Measure the terminal (TIOCGWINSZ) and populate the globals above. */
void tui_init_layout(void);

/* Enter raw mode + alt buffer + hide cursor. */
void tui_term_setup(void);

/* Inverse of tui_term_setup: restore termios, switch back to default
 * buffer, re-enable cursor. */
void tui_term_restore(void);
