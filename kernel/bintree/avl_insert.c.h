/* SPDX-License-Identifier: BSD-2-Clause */

#if BINTREE_PTR_FUNCS
   #define CMP(a, b) bintree_insrem_ptr_cmp(a, b, field_off)
#else
   #define CMP(a, b) objval_cmpfun(a, b)
#endif

#if BINTREE_PTR_FUNCS
bool
bintree_insert_ptr_internal(void **root_obj_ref,
                            void *obj,
                            ptrdiff_t bintree_offset,
                            ptrdiff_t field_off)
#else
bool
bintree_insert_internal(void **root_obj_ref,
                        void *obj,
                        cmpfun_ptr objval_cmpfun,
                        ptrdiff_t bintree_offset)
#endif
{
   ASSERT(root_obj_ref != NULL);

   if (!*root_obj_ref) {
      *root_obj_ref = obj;
      return true;
   }

   /*
    * It will contain the whole reverse path leaf to root objects traversed:
    * that is needed for the balance at the end (it simulates the stack
    * unwinding that happens for recursive implementations).
    */
   void **stack[MAX_TREE_HEIGHT] = {0};
   int stack_size = 0;
   sptr c;

   STACK_PUSH(root_obj_ref);

   while (true) {

      root_obj_ref = STACK_TOP();

      ASSERT(root_obj_ref != NULL);
      ASSERT(*root_obj_ref != NULL);

      bintree_node *root = OBJTN(*root_obj_ref);

      if (!(c = CMP(obj, *root_obj_ref)))
         return false; // such elem already exists.

      if (c < 0) {

         if (!root->left_obj) {
            root->left_obj = obj;
            BALANCE(&root->left_obj);
            BALANCE(root_obj_ref);
            break;
         }

         STACK_PUSH(&root->left_obj);
         continue;
      }

      // case c > 0

      if (!root->right_obj) {
         root->right_obj = obj;
         BALANCE(&root->right_obj);
         BALANCE(root_obj_ref);
         break;
      }

      STACK_PUSH(&root->right_obj);
   }

   while (stack_size > 0)
      BALANCE(STACK_POP());

   return true;
}

