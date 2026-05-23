/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>

#define ACPI_OK 0
#define MY_RET(x) do { int _s = (x); return _s; } while (0)

extern int do_a(void);
extern int do_b(int);

/*
 * Patterns that previously false-positived the rule:
 *   1. Fall-through case labels (body is the next CASE_STMT).
 *   2. Case body is a control-flow stmt whose braced body
 *      legitimately contains decls.
 *   3. Case body is a macro that expands to a GCC statement-
 *      expression `({ DECL; ...; result; })` -- the decls are
 *      scoped to that compound, not to the case label.
 */
int classify(int x)
{
   int rc;

   switch (x) {

      /* (1) fall-through to a properly-braced case body */
      case 1:
      case 2:
      case 3: {
         int local = do_b(x);
         rc = local + 1;
         break;
      }

      /* (2) body is an if-stmt with decls inside its braced body */
      case 10:
         if (do_a()) {
            bool local_flag = true;
            rc = local_flag ? 10 : 20;
         }
         break;

      /* (3) body is a macro that expands to a statement-expr */
      case 20:
         MY_RET(ACPI_OK);

      default:
         rc = 0;
         break;
   }

   return rc;
}
