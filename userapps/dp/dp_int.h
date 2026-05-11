/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Internal header for the userspace `dp` tool — panel registry +
 * main-loop state. Generic TUI primitives (key events, terminal
 * lifecycle, raw-emit, panel-area constants) live in
 * userapps/common/.
 */

#pragma once

#include <stdbool.h>

#include "tui_input.h"        /* struct key_event */
#include "tui_layout.h"       /* DP_W / DP_H + layout globals */

enum dp_kb_handler_action {
   dp_kb_handler_ok_and_stop     =  1,
   dp_kb_handler_ok_and_continue =  0,
   dp_kb_handler_nak             = -1,
};

typedef enum dp_kb_handler_action (*dp_keypress_func)(struct key_event);

/*
 * One panel registered with dp_register_screen(). The painter draws
 * buffer rows [0, static_rows) at fixed positions and the scrollable
 * region (rows >= static_rows) is offset by row_off. Default 0 means
 * "everything scrolls"; Tasks sets static_rows to the relrow of the
 * first task so the action menu + table header stay anchored as the
 * user scrolls through tasks.
 */
struct dp_screen {

   struct dp_screen *next;
   int index;
   const char *label;

   int static_rows;       /* pinned-at-top rows count */
   int row_off;           /* scroll offset within the scrollable region */
   int row_max;           /* highest panel-local relrow ever written */

   void (*first_setup)(void);
   void (*draw_func)(void);
   void (*on_dp_enter)(void);
   void (*on_dp_exit)(void);
   dp_keypress_func on_keypress_func;
};

/* Panel-loop state (defined in dp_panel.c). */
extern bool ui_need_update;
extern const char *modal_msg;
extern struct dp_screen *dp_ctx;

/* Screen registry + main loop (dp_main.c). */
void dp_register_screen(struct dp_screen *screen);
int  dp_run_panel(void);

/*
 * Tell the main loop that an overlay (modal, sub-screen) has trampled
 * over the chrome and panel content; the next iteration must repaint
 * everything from scratch. Used by the Tasks panel after the tracer
 * subprocess returns (the tracer's dp_term_restore reset alt buffer +
 * cursor + termios).
 */
void dp_force_full_redraw(void);

/* One-shot ps mode (screen_tasks.c → main.c via argv[0]). */
int  dp_run_ps(void);
