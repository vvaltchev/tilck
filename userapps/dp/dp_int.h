/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Internal header for the userspace `dp` tool. Mirrors the kernel
 * modules/debugpanel/dp_int.h but adapted for userland: no fs_handles,
 * no kernel-side scancode constants, key codes are dp-private opaque
 * tags (DP_KEY_*), and `struct key_event` is a plain POD.
 */

#pragma once

#include <stdbool.h>

/* Inner panel area in characters (matches the kernel value). */
#define DP_W   76
#define DP_H   23

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
 * variant forms — see dp_input.c), and screens compare them with these
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

enum dp_kb_handler_action {
   dp_kb_handler_ok_and_stop     =  1,
   dp_kb_handler_ok_and_continue =  0,
   dp_kb_handler_nak             = -1,
};

typedef enum dp_kb_handler_action (*dp_keypress_func)(struct key_event);

/*
 * One panel. Phase 6 only uses `row_off` and `row_max` (consulted by
 * dp_write for scroll/overflow clipping); Phase 7 fills in the rest
 * and registers screens via dp_register_screen().
 */
struct dp_screen {

   /* List linkage and registry data — populated in Phase 7. */
   struct dp_screen *next;
   int index;
   const char *label;

   /*
    * Number of rows at the top of the panel that are pinned in place.
    * The painter draws buffer rows [0, static_rows) at fixed positions
    * and the scrollable region (rows >= static_rows) is offset by
    * row_off. Default 0 means "everything scrolls", which is what the
    * non-Tasks panels want; Tasks sets it to the relrow of the first
    * task so the action menu + table header stay anchored as the user
    * scrolls through tasks.
    */
   int static_rows;

   /*
    * Scroll offset within the SCROLLABLE region (i.e., rows
    * [static_rows, row_max]). row_off=0 means the first scrollable
    * row sits just below the static area; row_off=K means K rows of
    * the scrollable region have been scrolled past the top of the
    * scrollable viewport. The static area is unaffected.
    */
   int row_off;

   /* Highest panel-local relrow ever written by draw_func. */
   int row_max;

   void (*first_setup)(void);
   void (*draw_func)(void);
   void (*on_dp_enter)(void);
   void (*on_dp_exit)(void);
   dp_keypress_func on_keypress_func;
};

/* Layout state — initialized by dp_init_layout(). */
extern int dp_rows;
extern int dp_cols;
extern int dp_start_row;
extern int dp_end_row;
extern int dp_start_col;
extern int dp_screen_start_row;
extern int dp_screen_rows;

extern bool ui_need_update;
extern const char *modal_msg;
extern struct dp_screen *dp_ctx;

/* Layout + terminal lifecycle. */
void dp_init_layout(void);
void dp_term_setup(void);
void dp_term_restore(void);

/* Input. */
int  dp_read_ke_from_tty(struct key_event *ke);
int  dp_read_line(char *buf, int buf_size);
void dp_set_input_blocking(bool blocking);

/* Screen registry + main loop (dp_main.c). */
void dp_register_screen(struct dp_screen *screen);
int  dp_run_panel(void);

/* One-shot ps mode (screen_tasks.c). */
int  dp_run_ps(void);

/* Tracer mode (screen_tracing.c). */
int  dp_run_tracer(void);
