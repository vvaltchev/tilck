/* SPDX-License-Identifier: BSD-2-Clause */

extern void free_foo(void *);
extern void kfree_bar(void *);

void cleanup(void *a, void *b)
{
   if (a)
      free_foo(a);                  /* idiom error: unguarded form */

   if (b != NULL)
      kfree_bar(b);                 /* idiom error: != NULL guard */
}
