/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Panel-aware buffered-emit layer + modal overlay for the dp tool.
 * Functions here paint into a row-keyed in-memory buffer that
 * dp_main's main loop later flushes to the terminal via dp_buf_paint
 * (with scroll-offset and static-region semantics). They depend on
 * dp_ctx for clipping — that pointer is defined in dp_panel.c and
 * exists only in the `dp` binary.
 *
 * Do NOT include this header from userapps/common/: the buffered
 * macros dp_writeln / dp_writeln2 capture a local `row` variable and
 * are only meaningful inside the panel-screen rendering convention.
 */

#pragma once

#include <stdbool.h>

/* Append a formatted string at panel-local (row, col). */
void dp_write(int row, int col, const char *fmt, ...)
   __attribute__((format(printf, 3, 4)));

/*
 * Buffered-emit convenience macros. dp_writeln expects a local
 * `row` variable to be in scope (incremented per call); dp_writeln2
 * also captures `col`. The convention is one static `int row` per
 * screen-render TU, reset at the top of draw_func.
 *
 * WARNING: dirty macros expecting local `row` (and `col` for the
 * `2` variant) variables to be defined.
 */
#define dp_writeln(...)  dp_write(row++, 0, __VA_ARGS__)
#define dp_writeln2(...) dp_write(row++, col, __VA_ARGS__)

/*
 * Buffered-emit lifecycle. dp_main resets the buffer before each
 * draw_func, then paints it onto the panel content area after.
 * PAGE_UP/PAGE_DOWN scrolling repaints the same buffer at a different
 * row_off (no re-execution of draw_func).
 */
void dp_buf_reset(void);

void dp_buf_paint(int row_off,
                  int screen_rows,
                  int term_first_row,
                  int panel_left_col,
                  int panel_w,
                  int static_rows);

/*
 * Centered modal alert with "Press ANY key" hint. Direct-emit (paints
 * straight to STDOUT, outside the buffered panel content), so callers
 * must call dp_force_full_redraw afterwards to bring the panel back.
 */
void dp_show_modal_msg(const char *msg);
