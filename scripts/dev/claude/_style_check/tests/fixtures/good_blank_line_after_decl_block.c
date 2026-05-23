/* SPDX-License-Identifier: BSD-2-Clause */

extern void use_int(int);
extern int compute(int);

/*
 * GCC statement-expression macro. Expansion produces an inner
 * COMPOUND_STMT (decls + a result expression) pinned to the call
 * site -- the rule must not interpret that as a decl block.
 */
#define BUMP(a, b) \
   ({ \
      const int _a = (a); \
      const int _b = (b); \
      _a > _b ? _a : _b; \
   })

/*
 * Tight control-flow body: 1 decl + 1 use, conceptually one
 * operation. A blank line between the two would be uglier than
 * the absence of one. Rule must not fire here.
 */
void tight_if(int timeout)
{
   int dummy = 0;

   if (timeout > 0) {
      int ticks = BUMP(timeout / 7, 1);
      use_int(ticks);
   }

   (void)dummy;
}

/*
 * Sub-block to narrow a temporary's scope. Sub-blocks are tight by
 * convention -- rule must not fire on the inner COMPOUND_STMT.
 */
int with_subblock(int x)
{
   int a = compute(x);

   {
      int tmp = a * 2;
      return tmp + a;
   }
}

/*
 * Function-body uses a statement-expression macro in a single
 * decl. The `({ ... })` inside BUMP expands to an inner
 * COMPOUND_STMT pinned to the macro call site; the rule must
 * not mistake the macro's inner compound stmt for a decl
 * block. The outer function-body has the conventional blank
 * line after its decl.
 */
int single_macro_use(int x)
{
   int v = BUMP(x, 0);

   return v;
}
