
#include <bintree.h>

#define ALLOWED_IMBALANCE 1

static inline bintree_node *
obj_to_bintree_node(void *obj, ptrdiff_t offset)
{
   return obj ? (bintree_node *)((char*)obj + offset) : NULL;
}

static inline void *
bintree_node_to_obj(bintree_node *node, ptrdiff_t offset)
{
   return node ? (void *)((char*)node - offset) : NULL;
}

#define OBJTN(o) (obj_to_bintree_node((o), bintree_offset))
#define NTOBJ(n) (bintree_node_to_obj((n), bintree_offset))

#define LEFT_OF(obj) ( OBJTN((obj))->left_obj )
#define RIGHT_OF(obj) ( OBJTN((obj))->right_obj )
#define HEIGHT(obj) ((obj) ? OBJTN((obj))->height : -1)

static inline void
update_height(bintree_node *node, ptrdiff_t bintree_offset)
{
   node->height = MAX(HEIGHT(node->left_obj), HEIGHT(node->right_obj)) + 1;
}

#define UPDATE_HEIGHT(n) update_height((n), bintree_offset)



/*
 * rotate the left child of *obj clock-wise
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
   ASSERT(orig_node->left_obj != NULL);

   bintree_node *orig_left_child = OBJTN(orig_node->left_obj);
   *obj = orig_node->left_obj;
   orig_node->left_obj = orig_left_child->right_obj;
   OBJTN(*obj)->right_obj = NTOBJ(orig_node);

   UPDATE_HEIGHT(orig_node);
   UPDATE_HEIGHT(orig_left_child);
}

/*
 * rotate the right child of obj counterclock-wise
 */

void rotate_ccw_right_child(void **obj, ptrdiff_t bintree_offset)
{
   ASSERT(obj != NULL);
   ASSERT(*obj != NULL);

   bintree_node *orig_node = OBJTN(*obj);
   ASSERT(orig_node->right_obj != NULL);

   bintree_node *orig_right_child = OBJTN(orig_node->right_obj);
   *obj = orig_node->right_obj;
   orig_node->right_obj = orig_right_child->left_obj;
   OBJTN(*obj)->left_obj = NTOBJ(orig_node);

   UPDATE_HEIGHT(orig_node);
   UPDATE_HEIGHT(orig_right_child);
}


void rotate_left_right(void **obj, ptrdiff_t bintree_offset)
{
   rotate_ccw_right_child(&LEFT_OF(*obj), bintree_offset);
   rotate_cw_left_child(obj, bintree_offset);
}

void rotate_right_left(void **obj, ptrdiff_t bintree_offset)
{
   rotate_cw_left_child(&RIGHT_OF(*obj), bintree_offset);
   rotate_ccw_right_child(obj, bintree_offset);
}


void bintree_avl_balance(void **obj, ptrdiff_t bintree_offset)
{
   ASSERT(obj != NULL);

   if (*obj == NULL)
      return;

   void *left_obj = LEFT_OF(*obj);
   void *right_obj = RIGHT_OF(*obj);

   int bf = HEIGHT(left_obj) - HEIGHT(right_obj);

   if (bf > ALLOWED_IMBALANCE) {

      if (HEIGHT(LEFT_OF(left_obj)) >= HEIGHT(RIGHT_OF(left_obj))) {
         rotate_cw_left_child(obj, bintree_offset);
      } else {
         rotate_left_right(obj, bintree_offset);
      }

   } else if (bf < -ALLOWED_IMBALANCE) {

      if (HEIGHT(RIGHT_OF(right_obj)) >= HEIGHT(LEFT_OF(right_obj))) {
         rotate_ccw_right_child(obj, bintree_offset);
      } else {
         rotate_right_left(obj, bintree_offset);
      }
   }

   UPDATE_HEIGHT(OBJTN(*obj));
}

bool
bintree_insert_internal(void **root_obj,
                        void *obj,
                        cmpfun_ptr cmp,
                        ptrdiff_t bintree_offset)
{
   ASSERT(root_obj != NULL);
   ASSERT(*root_obj != NULL);

   bintree_node *root = OBJTN(*root_obj);

   bool ret = true;
   int c = cmp(obj, *root_obj);

   if (c == 0) {
      return false; // such elem already exists.
   }

   if (c < 0) {

      if (!root->left_obj) {
         root->left_obj = obj;
         bintree_avl_balance(&root->left_obj, bintree_offset);
         goto end;
      }

      ret = bintree_insert_internal(&root->left_obj, obj, cmp, bintree_offset);
      goto end;
   }

   // case c > 0

   if (!root->right_obj) {
      root->right_obj = obj;
      bintree_avl_balance(&root->right_obj, bintree_offset);
      goto end;
   }

   ret = bintree_insert_internal(&root->right_obj, obj, cmp, bintree_offset);

end:
   bintree_avl_balance(root_obj, bintree_offset);
   return ret;
}
