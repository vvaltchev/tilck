
#include <bintree.h>

static inline int height(bintree_node *n)
{
   return n ? n->height : -1;
}


static void update_height(bintree_node *n)
{
   n->height = MAX(height(n->left), height(n->right)) + 1;
}


/*
 * rotate the left child of 'n' clock-wise
 *
 *         (n)                  (nl)
 *         /  \                 /  \
 *       (nl) (nr)   ==>    (nll)  (n)
 *       /  \                     /   \
 *    (nll) (nlr)               (nlr) (nr)
 */

void rotate_cw_left_child(bintree_node *n)
{

}



bool
bintree_insert_internal(void **root_obj,
                        void *obj,
                        cmpfun_ptr cmp,
                        ptrdiff_t bintree_offset)
{
   bintree_node *root = (bintree_node*) ((char*)*root_obj + bintree_offset);

   bool ret = true;
   int c = cmp(obj, *root_obj);

   if (c == 0) {
      return false; // such elem already exists.
   }

   if (c < 0) {

      if (!root->left) {
         root->left = obj;
         //balance(root->left);
         goto end;
      }

      ret = bintree_insert_internal(&root->left, obj, cmp, bintree_offset);
      goto end;
   }

   // case c > 0

   if (!root->right) {
      root->right = obj;
      //balance(root->right);
      goto end;
   }

   ret = bintree_insert_internal(&root->right, obj, cmp, bintree_offset);

end:
   //balance(root);
   return ret;
}
