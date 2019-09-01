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

#include <tilck/kernel/bintree.h>

typedef struct {

   bintree_node node;
   size_t size;
   size_t count;

} kmalloc_acc_alloc;

static size_t alloc_arr_size;
static size_t alloc_arr_used;
static kmalloc_acc_alloc *alloc_arr;
static kmalloc_acc_alloc *alloc_tree_root;

static void kmalloc_init_heavy_stats(void)
{
   alloc_arr_size = 1024;
   alloc_arr_used = 0;
   alloc_arr = kmalloc(sizeof(kmalloc_acc_alloc) * alloc_arr_size);

   if (!alloc_arr)
      panic("Unable to alloc memory for the kmalloc heavy stats");

   printk("[kmalloc] Heavy stats enabled\n");
}

static void kmalloc_account_alloc(size_t size)
{
   kmalloc_acc_alloc *obj;

   if (!alloc_arr)
      return;

   obj = bintree_find_ptr(alloc_tree_root, size, kmalloc_acc_alloc, node, size);

   if (obj) {
      obj->count++;
      return;
   }

   if (alloc_arr_used == alloc_arr_size)
      panic("[kmalloc] No more space in alloc_arr");

   obj = &alloc_arr[alloc_arr_used++];
   bintree_node_init(&obj->node);
   obj->size = size;
   obj->count = 1;

   DEBUG_CHECKED_SUCCESS(
      bintree_insert_ptr(&alloc_tree_root, obj, kmalloc_acc_alloc, node, size)
   );
}

void debug_kmalloc_chunks_stats_start_read(debug_kmalloc_chunks_ctx *ctx)
{
   if (!KMALLOC_HEAVY_STATS)
      return;

   ASSERT(!is_preemption_enabled());
   bintree_in_order_visit_start(ctx,
                                alloc_tree_root,
                                kmalloc_acc_alloc,
                                node,
                                true);
}

bool
debug_kmalloc_chunks_stats_next(debug_kmalloc_chunks_ctx *ctx,
                                size_t *size, size_t *count)
{
   if (!KMALLOC_HEAVY_STATS)
      return false;

   ASSERT(!is_preemption_enabled());

   kmalloc_acc_alloc *obj;
   obj = bintree_in_order_visit_next(ctx);

   if (!obj)
      return false;

   *size = obj->size;
   *count = obj->count;
   return true;
}

