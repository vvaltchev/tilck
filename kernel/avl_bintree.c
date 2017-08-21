
#include <bintree.h>

static inline int height(bintree_node *n)
{
   return n ? n->height : -1;
}


static void update_height(bintree_node *n)
{
   n->height = MAX(height(n->left), height(n->right)) + 1;
}


static inline bintree_node *
obj_to_bintree_node(void *obj, ptrdiff_t bintree_offset)
{
   ASSERT(obj != NULL);
   return (bintree_node *)((char*)obj + bintree_offset);
}

static inline void *
bintree_node_to_obj(bintree_node *node, ptrdiff_t bintree_offset)
{
   ASSERT(node != NULL);
   return (void *)((char*)node - bintree_offset);
}

#define OBJTN(o) (obj_to_bintree_node((o), bintree_offset))
#define NTOBJ(n) (bintree_node_to_obj((n), bintree_offset))


/*
 * rotate the left child of obj clock-wise
 *
 *         (n)                  (nl)
 *         /  \                 /  \
 *       (nl) (nr)   ==>    (nll)  (n)
 *       /  \                     /   \
 *    (nll) (nlr)               (nlr) (nr)
 */

void rotate_cw_left_child(void **obj, ptrdiff_t bintree_offset)
{
   ASSERT(obj != NULL);
   ASSERT(*obj != NULL);

   bintree_node *orig_node = OBJTN(*obj);
   ASSERT(orig_node->left != NULL);

   bintree_node *orig_left_child = OBJTN(orig_node->left);
   *obj = orig_node->left;
   orig_node->left = orig_left_child->right;
   OBJTN(*obj)->right = NTOBJ(orig_node);

   update_height(orig_node);
   update_height(orig_left_child);
}

/*
 * rotate the right child of obj counterclock-wise
 */

void rotate_ccw_right_child(void **obj, ptrdiff_t bintree_offset)
{
   ASSERT(obj != NULL);
   ASSERT(*obj != NULL);

   bintree_node *orig_node = OBJTN(*obj);
   ASSERT(orig_node->right != NULL);

   bintree_node *orig_right_child = OBJTN(orig_node->right);
   *obj = orig_node->right;
   orig_node->right = orig_right_child->left;
   OBJTN(*obj)->left = NTOBJ(orig_node);

   update_height(orig_node);
   update_height(orig_right_child);
}


bool
bintree_insert_internal(void **root_obj,
                        void *obj,
                        cmpfun_ptr cmp,
                        ptrdiff_t bintree_offset)
{
   ASSERT(root_obj != NULL);
   ASSERT(*root_obj != NULL);

   bintree_node *root = obj_to_bintree_node(*root_obj, bintree_offset);

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
