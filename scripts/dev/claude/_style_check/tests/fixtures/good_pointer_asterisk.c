/* SPDX-License-Identifier: BSD-2-Clause */

#include <stddef.h>

struct foo {
   int *attached_field;
   const char *also_attached;
};

/* Function pointer -- exempt from the attached-asterisk rule. */
typedef int (*work_fn)(int);

/* Pointer to pointer also exempt from the simple rule. */
extern int **g_pp;

int helper(int *attached_param, const struct foo *cfg)
{
   int *local = attached_param;
   work_fn fn = NULL;

   (void)fn;
   (void)cfg;
   return local ? *local : 0;
}
