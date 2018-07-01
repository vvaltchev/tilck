#pragma once

#include <exos/hal.h>
#include <exos/sync.h>

extern kcond kb_cond;

void init_kb();

bool kb_cbuf_is_empty(void);
bool kb_cbuf_is_full(void);
char kb_cbuf_read_elem(void);
