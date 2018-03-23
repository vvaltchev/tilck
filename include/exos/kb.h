#pragma once

#include <exos/hal.h>
#include <exos/sync.h>

extern kcond kb_cond;

void init_kb();
void keyboard_handler(regs *r);

bool kb_cbuf_is_empty(void);
bool kb_cbuf_is_full(void);
void kb_cbuf_write_elem(char c);
char kb_cbuf_read_elem(void);
