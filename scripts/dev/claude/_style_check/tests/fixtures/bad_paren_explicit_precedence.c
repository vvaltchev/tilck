/* SPDX-License-Identifier: BSD-2-Clause */

extern unsigned long g_base;
extern unsigned long g_mask;
extern unsigned long g_flag_bits;

unsigned long compute_addr(unsigned long shift)
{
   /* Mixed shift + bitwise without parens: precedence-fragile. */
   return g_base << shift & g_mask | g_flag_bits;
}
