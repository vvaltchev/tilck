/* SPDX-License-Identifier: BSD-2-Clause */

#if BINTREE_INT_FUNCS
   #define CMP(a, b) bintree_insrem_int_cmp(a, b, field_off)
#else
   #define CMP(a, b) objval_cmpfun(a, b)
#endif

#if BINTREE_INT_FUNCS
void *
bintree_remove_int_internal(void **root_obj_ref,
                            void *value_ptr,
                            ptrdiff_t bintree_offset,
                            ptrdiff_t field_off)
#else
void *
bintree_remove_internal(void **root_obj_ref,
                        void *value_ptr,
                        cmpfun_ptr objval_cmpfun, // cmp(root_obj, value_ptr)
                        ptrdiff_t bintree_offset)
#endif
{
   void **stack[MAX_TREE_HEIGHT] = {0};
   int stack_size = 0;

   ASSERT(root_obj_ref != NULL);
   STACK_PUSH(root_obj_ref);

   while (true) {

      root_obj_ref = STACK_TOP();

      if (!*root_obj_ref)
         return NULL; // we did not find the object.

      sptr c = CMP(*root_obj_ref, value_ptr);

      if (!c)
         break;

      // *root_obj_ref is smaller then val => val is bigger => go right.
      STACK_PUSH(c < 0 ? &RIGHT_OF(*root_obj_ref) : &LEFT_OF(*root_obj_ref));
   }

   void *deleted_obj = *root_obj_ref;
   bintree_remove_internal_aux(root_obj_ref, stack, stack_size, bintree_offset);
   return deleted_obj;
}

#undef CMP
