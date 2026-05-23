/* SPDX-License-Identifier: BSD-2-Clause */

extern int some_pred(int);
extern void short_call(int);

void short_calls(int n)
{
   short_call(1);
   short_call(2);
   short_call(3);
   short_call(4);
   if (some_pred(n) && n > 0 && n < 100)  short_call(n) + short_call(n+1);
   short_call(5);
   short_call(6);
   short_call(7);
   short_call(8);
   short_call(9);
}
