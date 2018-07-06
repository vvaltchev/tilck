#pragma once

#include <exos/kernel/hal.h>
#include <exos/kernel/sync.h>
#include <exos/kernel/kb_scancode_set1_keys.h>

#define KB_HANDLER_OK_AND_STOP       1
#define KB_HANDLER_OK_AND_CONTINUE   0
#define KB_HANDLER_NAK              -1

typedef int (*keypress_func)(u32, u8);

void init_kb(void);
bool kb_is_pressed(u32 key);
int kb_register_keypress_handler(keypress_func f);

static inline bool kb_is_ctrl_pressed(void)
{
   return kb_is_pressed(KEY_LEFT_CTRL) || kb_is_pressed(KEY_RIGHT_CTRL);
}

static inline bool kb_is_alt_pressed(void)
{
   return kb_is_pressed(KEY_LEFT_ALT) || kb_is_pressed(KEY_RIGHT_ALT);
}

static inline bool kb_is_ctrl_or_alt_pressed(void)
{
   return kb_is_ctrl_pressed() | kb_is_alt_pressed();
}
