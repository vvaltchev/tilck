/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _KMALLOC_C_

   #error This is NOT a header file and it is not meant to be included

   /*
    * The only purpose of this file is to keep kmalloc.c shorter.
    * Yes, this file could be turned into a regular C source file, but at the
    * price of making several static functions and variables in kmalloc.c to be
    * just non-static. We don't want that. Code isolation is a GOOD thing.
    */

#endif

void
kmalloc_create_accelerator(kmalloc_accelerator *a, u32 elem_size, u32 elem_c)
{
   /* The both elem_size and elem_count must be a power of 2 */
   ASSERT(roundup_next_power_of_2(elem_size) == elem_size);
   ASSERT(roundup_next_power_of_2(elem_c) == elem_c);
   ASSERT(elem_size <= KMALLOC_FL_SUB_BLOCK_MIN_SIZE_MASK);

   if (elem_size <= SMALL_HEAP_MAX_ALLOC) {

      /*
       * All the elements must live in a "small heap". The problem with that
       * is that elem_size * elem_c must be <= SMALL_HEAP_SIZE. Otherwise,
       * it's possible nor to use bigger heaps (because their MBS (min block
       * size) is too big), neither to use small heaps because they cannot
       * contain `elem_c` elements of size `elem_size`.
       *
       * Fix: just reduce the value of `elem_c`.
       */

      ASSERT(elem_size * elem_c <= (SMALL_HEAP_SIZE - SMALL_HEAP_MD_SIZE));
   }

   *a = (kmalloc_accelerator) {
      .elem_size = elem_size,
      .elem_count = elem_c,
      .curr_elem = elem_c,
      .buf = NULL,
   };
}

void *
kmalloc_accelerator_get_elem(kmalloc_accelerator *a)
{
   size_t actual_size;

   if (a->curr_elem == a->elem_count) {

      actual_size = a->elem_size * a->elem_count;

      a->buf = general_kmalloc(&actual_size,   /* size (in/out)       */
                               a->elem_size);  /* sub_blocks_min_size */

      ASSERT(actual_size == a->elem_size * a->elem_count);

      if (!a->buf)
         return NULL;

      a->curr_elem = 0;
   }

   return a->buf + (a->elem_size * a->curr_elem++);
}

void
kmalloc_destroy_accelerator(kmalloc_accelerator *a)
{
   size_t actual_size;

   for (; a->curr_elem < a->elem_count; a->curr_elem++) {

      actual_size = a->elem_size;

      general_kfree(a->buf + (a->curr_elem * a->elem_size), /* ptr           */
                    &actual_size,                           /* size (in/out) */
                    KFREE_FL_ALLOW_SPLIT);

      ASSERT(actual_size == a->elem_size);
   }
}
