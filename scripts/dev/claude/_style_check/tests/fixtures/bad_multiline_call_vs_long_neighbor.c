/* SPDX-License-Identifier: BSD-2-Clause */

extern int setup(int, int, int, int, int, int, int, int, int, int);
extern void configure(int);

void example(int a, int b, int c)
{
   setup(a, b, c, a + b, a + c, b + c, a + b + c, a * b, b * c, a * c);
   configure(a |
             b |
             c);
   setup(a, b, c, a + b, a + c, b + c, a + b + c, a * b, b * c, a * c);
}
