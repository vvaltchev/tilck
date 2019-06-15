/* SPDX-License-Identifier: BSD-2-Clause */

#if BINTREE_INT_FUNCS
   #define CMP(a, b) bintree_find_int_cmp(a, b, field_off)
#else
   #define CMP(a, b) objval_cmpfun(a, b)
#endif

#if BINTREE_INT_FUNCS
void *
bintree_find_int_internal(void *root_obj,
                          const void *value_ptr,
                          ptrdiff_t bintree_offset,
                          ptrdiff_t field_off)
#else
void *
bintree_find_internal(void *root_obj,
                      const void *value_ptr,
                      cmpfun_ptr objval_cmpfun,
                      ptrdiff_t bintree_offset)
#endif
{
   while (root_obj) {

      sptr c = CMP(root_obj, value_ptr);

      if (c == 0)
         return root_obj;

      // root_obj is smaller then val => val is bigger => go right.
      root_obj = c < 0 ? RIGHT_OF(root_obj) : LEFT_OF(root_obj);
   }

   return NULL;
}

#undef CMP
