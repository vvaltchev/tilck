/* SPDX-License-Identifier: BSD-2-Clause */

#if BINTREE_PTR_FUNCS
   #define CMP(a, b) bintree_insrem_ptr_cmp(a, b, field_off)
#else
   #define CMP(a, b) objval_cmpfun(a, b)
#endif

#if BINTREE_PTR_FUNCS
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

   while (stack_size > 0)
      BALANCE(STACK_POP());

   return true;
}

