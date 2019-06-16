/* SPDX-License-Identifier: BSD-2-Clause */

#if BINTREE_PTR_FUNCS
   #define CMP(a, b) bintree_insrem_ptr_cmp(a, b, field_off)
#else
   #define CMP(a, b) objval_cmpfun(a, b)
#endif

#if BINTREE_PTR_FUNCS
bool
bintree_insert_ptr_internal(void **root_obj_ref,
                            void *new_obj,
                            ptrdiff_t bintree_offset,
                            ptrdiff_t field_off)
#else
bool
bintree_insert_internal(void **root_obj_ref,
                        void *new_obj,
                        cmpfun_ptr objval_cmpfun,
                        ptrdiff_t bintree_offset)
#endif
{
   ASSERT(root_obj_ref != NULL);

   if (!*root_obj_ref) {
      *root_obj_ref = new_obj;
      return true;
   }

   /*
    * It will contain the whole reverse path leaf to root objects traversed:
    * that is needed for the balance at the end (it simulates the stack
    * unwinding that happens for recursive implementations).
    */
   void **stack[MAX_TREE_HEIGHT];
   int stack_size = 0;
   sptr c;

   STACK_PUSH(root_obj_ref);

   while (*STACK_TOP()) {

      void **obj_ref = STACK_TOP();
      bintree_node *node = OBJTN(*obj_ref);

      if (!(c = CMP(new_obj, *obj_ref)))
         return false; // such elem already exists.

      STACK_PUSH(c < 0 ? &node->left_obj : &node->right_obj);
   }

   /* Place our object in its right destination */
   *STACK_TOP() = new_obj;

   while (stack_size > 0)
      BALANCE(STACK_POP());

   return true;
}

