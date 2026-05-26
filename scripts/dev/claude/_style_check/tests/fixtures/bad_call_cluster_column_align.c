/* SPDX-License-Identifier: BSD-2-Clause */

extern void set_val(int key, int val);

void setup(void)
{
   set_val(1,   10);
   set_val(2,   20);
   set_val(333, 30);
   set_val(4,  40);
}
