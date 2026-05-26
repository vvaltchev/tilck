/* SPDX-License-Identifier: BSD-2-Clause */

extern void do_a(void);
extern void do_b(void);

void foo(void)
{
   while (1) {
      do_a();
   }
}

void bar(void)
{
   for (;;) {
      do_b();
   }
}
