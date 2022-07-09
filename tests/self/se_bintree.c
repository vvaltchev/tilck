/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/bintree.h>
#include <tilck/kernel/list.h>

#include "se_data.h"

struct simple_obj {

   struct bintree_node bnode;
   struct list_node lnode;
   ulong value;
};

static long sobj_cmp(const void *a, const void *value)
{
   const struct simple_obj *e1 = a;
   return (long)e1->value - *(const long*)value;
}

NO_INLINE static struct simple_obj *
find_obj_with_bst(struct bintree_node *bst_root, ulong value)
{
   return bintree_find_ptr(bst_root, value, struct simple_obj, bnode, value);
}

NO_INLINE static struct simple_obj *
find_obj_with_bst2(struct bintree_node *bst_root, ulong value)
{
   return bintree_find(bst_root, &value, &sobj_cmp, struct simple_obj, bnode);
}

NO_INLINE static struct simple_obj *
find_obj_with_list(struct list *nodes_list_ref, ulong value)
{
   struct simple_obj *pos;

   list_for_each_ro(pos, nodes_list_ref, lnode) {

      if (pos->value == value)
         return pos;
   }

   return NULL;
}

static void
do_bintree_perf_test(u32 elems,
                     bool lookup,
                     struct simple_obj *(*bst_lookup)(
                        struct bintree_node *, ulong
                     ))
{
   struct bintree_node *bst_root = NULL;
   struct list nodes_list;
   struct simple_obj *obj;
   struct simple_obj *nodes;
   u64 start, duration;
   u32 bst_cycles, list_cycles;
   const u32 iters = 100;

   VERIFY(elems <= RANDOM_VALUES_COUNT);

   kernel_yield();

   if (se_is_stop_requested())
      return;

   nodes = kalloc_array_obj(struct simple_obj, elems);

   if (!nodes)
      panic("No enough memory to alloc `nodes`");

   list_init(&nodes_list);

   for (u32 i = 0; i < elems; i++) {
      bintree_node_init(&nodes[i].bnode);
      list_node_init(&nodes[i].lnode);
      nodes[i].value = random_values[i];
   }

   disable_preemption();

   start = RDTSC();

   for (u32 i = 0; i < elems; i++) {
      bintree_insert_ptr(&bst_root,
                         &nodes[i],
                         struct simple_obj,
                         bnode,
                         value);
   }

   duration = RDTSC() - start;
   bst_cycles = (u32)(duration / elems);

   start = RDTSC();

   for (u32 i = 0; i < elems; i++) {
      list_add_tail(&nodes_list, &nodes[i].lnode);
   }

   duration = RDTSC() - start;
   list_cycles = (u32)(duration / elems);

   if (lookup) {
      start = RDTSC();

      for (u32 j = 0; j < iters; j++) {
         for (u32 i = 0; i < elems; i++) {
            obj = bst_lookup(bst_root, random_values[i]);
            ASSERT(obj != NULL); (void)obj;
         }
      }

      duration = RDTSC() - start;
      bst_cycles = (u32)(duration / (iters * elems));

      start = RDTSC();

      for (u32 j = 0; j < iters; j++) {
         for (u32 i = 0; i < elems; i++) {
            obj = find_obj_with_list(&nodes_list, random_values[i]);
            ASSERT(obj != NULL); (void)obj;
         }
      }

      duration = RDTSC() - start;
      list_cycles = (u32)(duration / (iters * elems));
   }

   enable_preemption();

   printk("    %5u    |   %5u    |    %5u    \n",
          elems, bst_cycles, list_cycles);

   kfree_array_obj(nodes, struct simple_obj, elems);
}

void
selftest_bintree_perf(void)
{
   static const u32 lookup_elems[] = {
      10, 15, 20, 25, 35, 50, 75, 100, 250, 500, 1000
   };

   printk("Tilck's BST ptr-lookup performance compared to linked list\n");
   printk("\n");
   printk("    elems    |     bst    |     list\n");
   printk("-------------+------------+--------------\n");

   for (int i = 0; i < ARRAY_SIZE(lookup_elems); i++) {

      do_bintree_perf_test(lookup_elems[i], true, find_obj_with_bst);

      if (se_is_stop_requested())
         return;
   }

   printk("\n");
   printk("Tilck's BST generic-lookup performance compared to linked list\n");
   printk("\n");
   printk("    elems    |     bst    |     list\n");
   printk("-------------+------------+--------------\n");

   for (int i = 0; i < ARRAY_SIZE(lookup_elems); i++) {

      do_bintree_perf_test(lookup_elems[i], true, find_obj_with_bst2);

      if (se_is_stop_requested())
         return;
   }

   printk("\n");
   printk("Tilck's BST insert performance compared to linked list\n");
   printk("\n");
   printk("    elems    |     bst    |     list\n");
   printk("-------------+------------+--------------\n");
   do_bintree_perf_test(250, false, NULL);
   do_bintree_perf_test(500, false, NULL);
   do_bintree_perf_test(1000, false, NULL);
   printk("\n");
   printk("\n");
}

REGISTER_SELF_TEST(bintree_perf, se_med, &selftest_bintree_perf)
