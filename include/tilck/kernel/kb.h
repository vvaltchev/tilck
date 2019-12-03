/* SPDX-License-Identifier: BSD-2-Clause */
#pragma once

#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/kb_scancode_set1_keys.h>

struct kb_dev;

struct key_event {

   bool pressed : 1;
   u32 key : 23;
   char print_char : 8;
};

STATIC_ASSERT(sizeof(struct key_event) == sizeof(u32));

enum kb_handler_action {
   kb_handler_ok_and_stop = 1,
   kb_handler_ok_and_continue = 0,
   kb_handler_nak = -1,
};

typedef enum kb_handler_action
(*keypress_func)(struct kb_dev *, struct key_event);

struct keypress_handler_elem {

   struct list_node node;
   keypress_func handler;
};

struct kb_dev {

   struct list_node node;
   const char *driver_name;

   bool (*is_pressed)(u32 key);
   void (*register_handler)(struct keypress_handler_elem *e);
   bool (*scancode_to_ansi_seq)(u32 key, u8 modifiers, char *seq);
   u8 (*translate_to_mediumraw)(struct key_event ke);
};

void register_keyboard_device(struct kb_dev *kbdev);
void register_keypress_handler(struct keypress_handler_elem *e);
u8 kb_get_current_modifiers(struct kb_dev *kb);
int kb_get_fn_key_pressed(u32 key);

void init_kb(void); // TODO: remove

static ALWAYS_INLINE struct key_event
make_key_event(u32 key, char print_char, bool pressed)
{
   return (struct key_event) {
      .pressed = pressed,
      .key = key,
      .print_char = print_char,
   };
}

static inline bool kb_is_ctrl_pressed(struct kb_dev *kb)
{
   return kb->is_pressed(KEY_LEFT_CTRL) || kb->is_pressed(KEY_RIGHT_CTRL);
}

static inline bool kb_is_alt_pressed(struct kb_dev *kb)
{
   return kb->is_pressed(KEY_LEFT_ALT) || kb->is_pressed(KEY_RIGHT_ALT);
}

static inline bool kb_is_shift_pressed(struct kb_dev *kb)
{
   return kb->is_pressed(KEY_L_SHIFT) || kb->is_pressed(KEY_R_SHIFT);
}

#define KB_MOD_NONE   0
#define KB_MOD_ALT    1
#define KB_MOD_SHIFT  2
#define KB_MOD_CTRL   4

/*
 * Match is made by OR-ing the four defines above.
 */
static inline bool kb_exact_match_modifiers(struct kb_dev *kb, u8 match)
{
   return kb_get_current_modifiers(kb) == match;
}

static inline bool kb_partial_match_modifiers(struct kb_dev *kb, u8 match_mask)
{
   ASSERT(match_mask != 0);
   return !!(kb_get_current_modifiers(kb) & match_mask);
}
