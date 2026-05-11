/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Generic terminal-input layer for userspace TUIs: raw-mode stdin →
 * key_event tokens (single-byte chars or canonical xterm/VT100 ESC
 * sequences), plus a small cooked-mode line editor.
 *
 * No panel state, no dp/tracer specifics — any userspace tool that
 * runs the terminal in raw mode can link this.
 */

#pragma once

#include <stdbool.h>

/* Single-byte keys (raw byte values). */
#define DP_KEY_BACKSPACE   0x7f
#define DP_KEY_ESC         0x1b
#define DP_KEY_ENTER       0x0d
#define DP_KEY_CTRL_C      0x03
#define DP_KEY_CTRL_T      0x14

/*
 * Multi-byte function keys: the actual canonical ESC sequences emitted
 * by VT100/xterm-compatible terminals. dp_read_ke_from_tty() puts the
 * raw bytes into key_event.seq verbatim (after normalizing a couple of
 * variant forms — see tui_input.c), and screens compare them with these
 * macros via strcmp. There is no symbolic enum in between: stdin
 * already speaks the standard, so we use it directly.
 */
#define DP_KEY_UP          "\x1b[A"
#define DP_KEY_DOWN        "\x1b[B"
#define DP_KEY_RIGHT       "\x1b[C"
#define DP_KEY_LEFT        "\x1b[D"
#define DP_KEY_HOME        "\x1b[H"
#define DP_KEY_END         "\x1b[F"
#define DP_KEY_INS         "\x1b[2~"
#define DP_KEY_DEL         "\x1b[3~"
#define DP_KEY_PAGE_UP     "\x1b[5~"
#define DP_KEY_PAGE_DOWN   "\x1b[6~"

#define DP_KEY_SEQ_MAX     8

struct key_event {
   char print_char;             /* single-byte char, 0 if ESC sequence */
   char seq[DP_KEY_SEQ_MAX];    /* NUL-terminated raw ESC seq, "" if char */
};

/*
 * Read one key event from stdin. Returns 0 on success, < 0 if no input
 * was available (only meaningful in non-blocking mode).
 */
int dp_read_ke_from_tty(struct key_event *ke);

/*
 * Cooked-mode line editor: read a NUL-terminated line into buf (up to
 * buf_size-1 chars). Echoes typed chars, handles BACKSPACE. Returns the
 * length of the input.
 */
int dp_read_line(char *buf, int buf_size);

/* Toggle O_NONBLOCK on stdin. */
void dp_set_input_blocking(bool blocking);
