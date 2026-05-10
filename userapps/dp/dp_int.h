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

/* Single-byte keys (raw byte values, same as the kernel uses). */
#define DP_KEY_BACKSPACE   0x7f
#define DP_KEY_ESC         0x1b
#define DP_KEY_ENTER       0x0d
#define DP_KEY_CTRL_C      0x03
#define DP_KEY_CTRL_T      0x14

/*
 * Multi-byte / function-key codes used after parsing an ESC sequence.
 * The numeric values are dp-private — they need not match anything in
 * the kernel because dp_input.c here defines them and the screens read
 * them. Prefixed DP_ to avoid colliding with linux/input.h's KEY_*.
 */
#define DP_KEY_UP          1001
#define DP_KEY_DOWN        1002
#define DP_KEY_RIGHT       1003
#define DP_KEY_LEFT        1004
#define DP_KEY_HOME        1005
#define DP_KEY_INS         1006
#define DP_KEY_DEL         1007
#define DP_KEY_END         1008
#define DP_KEY_PAGE_UP     1009
#define DP_KEY_PAGE_DOWN   1010

struct key_event {
   bool pressed;
   unsigned key;        /* one of DP_KEY_* (multi-byte), 0 if printable */
   char print_char;     /* set if a printable single-byte char was typed */
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

   /* Scroll / overflow state, used by termutil's dp_write(). */
   int row_off;
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
