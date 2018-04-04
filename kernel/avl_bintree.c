
#include <exos/bintree.h>

#define MAX_TREE_HEIGHT 32
#define ALLOWED_IMBALANCE 1

#define STACK_PUSH(r) (stack[stack_size++] = (r))
#define STACK_TOP() (stack[stack_size-1])
#define STACK_POP() (stack[--stack_size])


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
 * rotate the left child of *obj_ref clock-wise
 *
 *         (n)                  (nl)
 *         /  \                 /  \
 *       (nl) (nr)   ==>    (nll)  (n)
 *       /  \                     /   \
 *    (nll) (nlr)               (nlr) (nr)
 */

void rotate_left_child(void **obj_ref, ptrdiff_t bintree_offset)
{
   ASSERT(obj_ref != NULL);
   ASSERT(*obj_ref != NULL);

   bintree_node *orig_node = OBJTN(*obj_ref);
   ASSERT(orig_node->left_obj != NULL);

   bintree_node *orig_left_child = OBJTN(orig_node->left_obj);
   *obj_ref = orig_node->left_obj;
   orig_node->left_obj = orig_left_child->right_obj;
   OBJTN(*obj_ref)->right_obj = NTOBJ(orig_node);

   UPDATE_HEIGHT(orig_node);
   UPDATE_HEIGHT(orig_left_child);
}

/*
 * rotate the right child of *obj_ref counterclock-wise (symmetric function)
 */

void rotate_right_child(void **obj_ref, ptrdiff_t bintree_offset)
{
   ASSERT(obj_ref != NULL);
   ASSERT(*obj_ref != NULL);

   bintree_node *orig_node = OBJTN(*obj_ref);
   ASSERT(orig_node->right_obj != NULL);

   bintree_node *orig_right_child = OBJTN(orig_node->right_obj);
   *obj_ref = orig_node->right_obj;
   orig_node->right_obj = orig_right_child->left_obj;
   OBJTN(*obj_ref)->left_obj = NTOBJ(orig_node);

   UPDATE_HEIGHT(orig_node);
   UPDATE_HEIGHT(orig_right_child);
}

#define ROTATE_CW_LEFT_CHILD(obj) (rotate_left_child((obj), bintree_offset))
#define ROTATE_CCW_RIGHT_CHILD(obj) (rotate_right_child((obj), bintree_offset))
#define BALANCE(obj) (balance((obj), bintree_offset))

static void balance(void **obj_ref, ptrdiff_t bintree_offset)
{
   ASSERT(obj_ref != NULL);

   if (*obj_ref == NULL)
      return;

   void *left_obj = LEFT_OF(*obj_ref);
   void *right_obj = RIGHT_OF(*obj_ref);

   int bf = HEIGHT(left_obj) - HEIGHT(right_obj);

   if (bf > ALLOWED_IMBALANCE) {

      if (HEIGHT(LEFT_OF(left_obj)) >= HEIGHT(RIGHT_OF(left_obj))) {
         ROTATE_CW_LEFT_CHILD(obj_ref);
      } else {
         ROTATE_CCW_RIGHT_CHILD(&LEFT_OF(*obj_ref));
         ROTATE_CW_LEFT_CHILD(obj_ref);
      }

   } else if (bf < -ALLOWED_IMBALANCE) {

      if (HEIGHT(RIGHT_OF(right_obj)) >= HEIGHT(LEFT_OF(right_obj))) {
         ROTATE_CCW_RIGHT_CHILD(obj_ref);
      } else {
         ROTATE_CW_LEFT_CHILD(&RIGHT_OF(*obj_ref));
         ROTATE_CCW_RIGHT_CHILD(obj_ref);
      }
   }

   UPDATE_HEIGHT(OBJTN(*obj_ref));
}


bool
bintree_insert_internal(void **root_obj_ref,
                        void *obj,
                        cmpfun_ptr cmp,
                        ptrdiff_t bintree_offset)
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

   STACK_PUSH(root_obj_ref);

   while (true) {

      root_obj_ref = STACK_TOP();

      ASSERT(root_obj_ref != NULL);
      ASSERT(*root_obj_ref != NULL);

      bintree_node *root = OBJTN(*root_obj_ref);

      int c = cmp(obj, *root_obj_ref);

      if (!c)
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

void *
bintree_find_internal(void *root_obj,
                      void *value_ptr,
                      cmpfun_ptr objval_cmpfun,
                      ptrdiff_t bintree_offset)

{
   while (root_obj) {

      int c = objval_cmpfun(root_obj, value_ptr);

      if (c == 0)
         return root_obj;

      // root_obj is smaller then val => val is bigger => go right.
      root_obj = c < 0 ? RIGHT_OF(root_obj) : LEFT_OF(root_obj);
   }

   return NULL;
}

void *
bintree_remove_internal(void **root_obj_ref,
                        void *value_ptr,
                        cmpfun_ptr objval_cmpfun, // cmp(root_obj, value_ptr)
                        ptrdiff_t bintree_offset)
{
   void **stack[MAX_TREE_HEIGHT] = {0};
   int stack_size = 0;

   ASSERT(root_obj_ref != NULL);
   STACK_PUSH(root_obj_ref);

   while (true) {

      root_obj_ref = STACK_TOP();

      if (!*root_obj_ref)
         return NULL; // we did not find the object.

      int c = objval_cmpfun(*root_obj_ref, value_ptr);

      if (!c)
         break;

      // *root_obj_ref is smaller then val => val is bigger => go right.
      STACK_PUSH(c < 0 ? &RIGHT_OF(*root_obj_ref) : &LEFT_OF(*root_obj_ref));
   }


   void *deleted_obj = *root_obj_ref;

   if (LEFT_OF(*root_obj_ref) && RIGHT_OF(*root_obj_ref)) {

      // not-leaf node

      void **left = &LEFT_OF(*root_obj_ref);
      void **right = &RIGHT_OF(*root_obj_ref);
      void **successor_ref = &RIGHT_OF(*root_obj_ref);

      int saved_stack_size = stack_size;

      while (LEFT_OF(*successor_ref)) {
         STACK_PUSH(successor_ref);
         successor_ref = &LEFT_OF(*successor_ref);
      }

      STACK_PUSH(successor_ref);

      // now *successor_ref is the smallest node at the right side of
      // *root_obj_ref and so it is its successor.

      // save *successor's right node (it has no left node!).
      void *successors_right = RIGHT_OF(*successor_ref); // may be NULL.

      // replace *root_obj_ref (to be deleted) with *successor_ref
      *root_obj_ref = *successor_ref;

      // now we have to replace *obj with its right child
      *successor_ref = successors_right;

      // balance the part of the tree up to the original value of 'obj'
      while (stack_size > saved_stack_size) {
         BALANCE(STACK_POP());
      }

      // restore root's original left and right links
      OBJTN(*root_obj_ref)->left_obj = *left;
      OBJTN(*root_obj_ref)->right_obj = *right;

   } else {

      // leaf node: replace with its left/right child.

      *root_obj_ref = LEFT_OF(*root_obj_ref)
                        ? LEFT_OF(*root_obj_ref)
                        : RIGHT_OF(*root_obj_ref);
   }

   while (stack_size > 0)
      BALANCE(STACK_POP());

   return deleted_obj;
}

// TODO: implement this without recursion

int
bintree_in_order_visit_internal(void *root_obj,
                                bintree_visit_cb visit_cb,
                                void *visit_cb_arg,
                                ptrdiff_t bintree_offset)
{
   int stop;

   if (!root_obj)
      return 0;

   stop = bintree_in_order_visit_internal(LEFT_OF(root_obj),
                                          visit_cb,
                                          visit_cb_arg,
                                          bintree_offset);

   if (stop)
      return stop;

   stop = visit_cb(root_obj, visit_cb_arg);

   if (stop)
      return stop;

   stop = bintree_in_order_visit_internal(RIGHT_OF(root_obj),
                                          visit_cb,
                                          visit_cb_arg,
                                          bintree_offset);

   return stop;
}
