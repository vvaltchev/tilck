/* SPDX-License-Identifier: BSD-2-Clause */
#include <stddef.h>
extern void kfree(void *, int);
extern void free(void *);

void cleanup(void *a, void *b)
{
   if (a)
      kfree(a, 32);

   if (b != NULL)
      free(b);
}
