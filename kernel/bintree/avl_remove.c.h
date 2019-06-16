/* SPDX-License-Identifier: BSD-2-Clause */

#if BINTREE_PTR_FUNCS
   #define CMP(a, b) bintree_insrem_ptr_cmp(a, b, field_off)
#else
   #define CMP(a, b) objval_cmpfun(a, b)
#endif

#if BINTREE_PTR_FUNCS
void *
bintree_remove_ptr_internal(void **root_obj_ref,
                            void *obj_or_value,
                            ptrdiff_t bintree_offset,
                            ptrdiff_t field_off)
#else
void *
bintree_remove_internal(void **root_obj_ref,
                        void *obj_or_value,
                        cmpfun_ptr objval_cmpfun,
                        ptrdiff_t bintree_offset)
#endif
{
   void **stack[MAX_TREE_HEIGHT];
   int stack_size = 0;
   void *deleted_obj;

   AVL_BUILD_PATH_TO_OBJ();
   deleted_obj = *STACK_TOP();

   if (!deleted_obj)
      return NULL;   /* element not found */

   bintree_remove_internal_aux(STACK_TOP(), stack, stack_size, bintree_offset);
   return deleted_obj;
}

#undef CMP
