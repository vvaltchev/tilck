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
 * Tiny function body (2 statements). Even at the function-body
 * level, a 2-or-3-statement body is a tight operation -- a blank
 * line between the decl and its use would be uglier than the
 * absence of one. Same pattern as boot/efi/intf.c:17-22's
 * `efi_boot_read_key()`. Rule must not fire here.
 */
int tiny_function_body(int x)
{
   int v = BUMP(x, 0);
   return v;
}

/*
 * 3-statement function body (decl + assign + return). Also a tight
 * operation -- still under SMALL_BODY_THRESHOLD. Rule must not fire.
 */
int three_stmt_body(int x)
{
   int v;
   v = BUMP(x, 0);
   return v;
}
