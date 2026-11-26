/* SPDX-License-Identifier: BSD-2-Clause */

extern int compute(int, int, int, int);

extern int extra_long_var_a, extra_long_var_b, extra_long_var_c;
extern int extra_long_var_d, extra_long_var_e, extra_long_var_f;
extern int extra_long_var_g;
/* Consistently-sized lines (70-75 cols): a 72-col one is no outlier. */
void consistent_block(void)
{
   int x_a = compute(111111, 222222, 333333, 444444) + extra_long_var_a;
   int x_b = compute(111111, 222222, 333333, 444444) + extra_long_var_b;
   int x_c = compute(111111, 222222, 333333, 444444) + extra_long_var_c;
   int x_d = compute(111111, 222222, 333333, 444444) + extra_long_var_d;
   int x_e = compute(111111, 222222, 333333, 444444) + extra_long_var_e;
   int x_f = compute(111111, 222222, 333333, 444444) + extra_long_var_f;
   int x_g = compute(111111, 222222, 333333, 444444) + extra_long_var_g;

   (void)x_a;
   (void)x_b;
}
