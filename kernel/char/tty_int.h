
#pragma once
#include <tilck/common/basic_defs.h>

extern struct termios c_term;
extern struct termios default_termios;

typedef enum {

   TERM_WFILTER_STATE_DEFAULT,
   TERM_WFILTER_STATE_ESC1,
   TERM_WFILTER_STATE_ESC2_CSI,
   TERM_WFILTER_STATE_ESC2_UNKNOWN

} term_write_filter_state_t;

typedef struct {

   term_write_filter_state_t state;
   char param_bytes[64];
   char interm_bytes[64];

   u8 pbc; /* param bytes count */
   u8 ibc; /* intermediate bytes count */

} term_write_filter_ctx_t;

extern term_write_filter_ctx_t term_write_filter_ctx;
extern u8 tty_curr_color;

void tty_input_init(void);
ssize_t tty_read(fs_handle fsh, char *buf, size_t size);
int tty_keypress_handler(u32 key, u8 c);
int tty_keypress_handler_int(u32 key, u8, bool check_mods);
int tty_term_write_filter(char c, u8 *color, term_action *a, void *ctx_arg);

