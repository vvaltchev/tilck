/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/kb.h>

struct list kb_devices_list = make_list(kb_devices_list);

void register_keyboard_device(struct kb_dev *kb)
{
   list_add_tail(&kb_devices_list, &kb->node);
}

void register_keypress_handler(struct keypress_handler_elem *e)
{
   struct kb_dev *kb;

   list_for_each_ro(kb, &kb_devices_list, node) {
      kb->register_handler(e);
   }
}

u8 kb_get_current_modifiers(struct kb_dev *kb)
{
   u8 shift = 1u * kb_is_shift_pressed(kb);
   u8 alt   = 2u * kb_is_alt_pressed(kb);
   u8 ctrl  = 4u * kb_is_ctrl_pressed(kb);

   /*
    * 0 nothing
    * 1 shift
    * 2 alt
    * 3 shift + alt
    * 4 ctrl
    * 5 shift + ctrl
    * 6 alt + ctrl
    * 7 shift + alt + ctrl
    */

   return shift + alt + ctrl;
}

/* NOTE: returns 0 if `key` not in [F1 ... F12] */
int kb_get_fn_key_pressed(u32 key)
{
   /*
    * We know that on the PC architecture, in the PS/2 set 1, keys F1-F12 have
    * all a scancode long 1 byte.
    */

   if (key >= 256)
      return 0;

   static const char fn_table[256] =
   {
      [KEY_F1]  =  1,
      [KEY_F2]  =  2,
      [KEY_F3]  =  3,
      [KEY_F4]  =  4,
      [KEY_F5]  =  5,
      [KEY_F6]  =  6,
      [KEY_F7]  =  7,
      [KEY_F8]  =  8,
      [KEY_F9]  =  9,
      [KEY_F10] = 10,
      [KEY_F11] = 11,
      [KEY_F12] = 12,
   };

   return fn_table[(u8) key];
}
