#pragma once

#include <exos/kernel/hal.h>
#include <exos/kernel/sync.h>

#define KB_HANDLER_OK_AND_STOP       1
#define KB_HANDLER_OK_AND_CONTINUE   0
#define KB_HANDLER_NAK              -1

typedef int (*keypress_func)(u32, u8);

void init_kb(void);
bool kb_is_pressed(u32 key);
int kb_register_keypress_handler(keypress_func f);
