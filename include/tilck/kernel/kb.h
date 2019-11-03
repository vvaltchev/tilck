/* SPDX-License-Identifier: BSD-2-Clause */
#pragma once

#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/kb_scancode_set1_keys.h>

#define KB_HANDLER_OK_AND_STOP       1
#define KB_HANDLER_OK_AND_CONTINUE   0
#define KB_HANDLER_NAK              -1

struct key_event {

   u32 key;
   char print_char;
   bool pressed;
};

typedef int (*keypress_func)(struct key_event);

struct keypress_handler_elem {

   struct list_node node;
   keypress_func handler;
};

void init_kb(void);
bool kb_is_pressed(u32 key);
void kb_register_keypress_handler(struct keypress_handler_elem *e);
bool kb_scancode_to_ansi_seq(u32 key, u8 modifiers, char *seq);
u8 kb_get_current_modifiers(void);
int kb_get_fn_key_pressed(u32 key);
u8 kb_translate_to_mediumraw(struct key_event ke);

static inline bool kb_is_ctrl_pressed(void)
{
   return kb_is_pressed(KEY_LEFT_CTRL) || kb_is_pressed(KEY_RIGHT_CTRL);
}

static inline bool kb_is_alt_pressed(void)
{
   return kb_is_pressed(KEY_LEFT_ALT) || kb_is_pressed(KEY_RIGHT_ALT);
}

static inline bool kb_is_shift_pressed(void)
{
   return kb_is_pressed(KEY_L_SHIFT) || kb_is_pressed(KEY_R_SHIFT);
}

#define KB_MOD_NONE   0
#define KB_MOD_ALT    1
#define KB_MOD_SHIFT  2
#define KB_MOD_CTRL   4

/*
 * Match is made by OR-ing the four defines above.
 */
static inline bool kb_exact_match_modifiers(u8 match)
{
   return kb_get_current_modifiers() == match;
}

static inline bool kb_partial_match_modifiers(u8 match_mask)
{
   ASSERT(match_mask != 0);
   return !!(kb_get_current_modifiers() & match_mask);
}
