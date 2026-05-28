/* SPDX-License-Identifier: BSD-2-Clause */

#include "avl_int.h"

#ifdef BINTREE_PTR_FUNCS
   #define CMP(a, b) bintree_insrem_ptr_cmp(a, b, field_off)
#else
   #define CMP(a, b) objval_cmpfun(a, b)
#endif

#ifdef BINTREE_PTR_FUNCS
bool
bintree_insert_ptr_internal(void **root_obj_ref,
                            void *obj_or_value,
                            long bintree_offset,
                            long field_off)
#else
bool
bintree_insert_internal(void **root_obj_ref,
                        void *obj_or_value,
                        cmpfun_ptr objval_cmpfun,
                        long bintree_offset)
#endif
{
   ASSERT(root_obj_ref != NULL);

   if (!*root_obj_ref) {
      *root_obj_ref = obj_or_value;
      return true;
   }

   /*
    * The stack will contain the whole reverse leaf-to-root path traversed
    * needed for the balance loop at the end, which simulates the stack
    * unwinding that happens for recursive implementations.
    */
   void **stack[MAX_TREE_HEIGHT];
   int stack_size = 0;
   void **dest;

   AVL_BUILD_PATH_TO_OBJ();
   dest = STACK_TOP();

   if (*dest)
      return false; /* element already existing */

   /* Place our object in its right destination */
   *dest = obj_or_value;

   /*
    * Walk back up the path, rebalancing each ancestor. Discard the new
    * node's own slot first: it's a freshly-initialized leaf (height 0,
    * no children) that needs no balancing, and the height-didn't-change
    * early-exit below would mis-fire on it -- short-circuiting the loop
    * before the real ancestors get their heights updated.
    *
    * AVL invariant after a single insert: at most one rotation is needed,
    * and once an ancestor's stored height doesn't change after rebalancing
    * (which includes the case after a rotation, where the subtree's height
    * is restored to its pre-insertion value), no further ancestor can be
    * affected. Stop there instead of walking all the way to the root.
    */
   stack_size--;

   while (stack_size > 0)
      if (!BALANCE(STACK_POP()))
         break;

   return true;
}

