
#pragma once
#include <exos/common/basic_defs.h>

extern struct termios c_term;
extern struct termios default_termios;

typedef enum {

   TERM_WFILTER_STATE_DEFAULT,
   TERM_WFILTER_STATE_ESC1,
   TERM_WFILTER_STATE_ESC2

} term_write_filter_state_t;

typedef struct {

   term_write_filter_state_t state;
   char param_bytes[16];
   char interm_bytes[16];

   u8 pbc; /* param bytes count */
   u8 ibc; /* intermediate bytes count */

} term_write_filter_ctx_t;

extern term_write_filter_ctx_t term_write_filter_ctx;

void tty_input_init(void);
ssize_t tty_read(fs_handle fsh, char *buf, size_t size);
int tty_term_write_filter(char c, u8 color, void *ctx_arg);

