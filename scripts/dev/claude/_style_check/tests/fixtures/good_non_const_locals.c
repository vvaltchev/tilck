/* SPDX-License-Identifier: BSD-2-Clause */

extern int compute(int);

int top_of_block(int x)
{
   int a, b;
   const int c = 42;            /* const trivially-static, top -- OK */

   a = compute(x);
   b = a + 1;

   /* Mid-function const local computed from in-flight state --
    * accepted by the rule (must skip CONST_QUALIFIED VAR_DECLs).
    */
   const int derived = a * b;

   return derived;
}

int with_subblock(int x)
{
   int a;

   a = compute(x);

   {
      /* Narrow scope: the sub-block creates a new COMPOUND_STMT in
       * which `tmp` is at the top. Must NOT trigger the rule. */
      int tmp = a * 2;
      return tmp + a;
   }
}
