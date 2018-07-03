#pragma once

#include <exos/hal.h>
#include <exos/sync.h>

void init_kb();

#define KB_HANDLER_OK_AND_STOP       1
#define KB_HANDLER_OK_AND_CONTINUE   0
#define KB_HANDLER_NAK              -1

typedef int (*keypress_func)(u32, u8);

int kb_register_keypress_handler(keypress_func f);
