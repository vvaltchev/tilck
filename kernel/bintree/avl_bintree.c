/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>
#include <tilck/kernel/bintree.h>

#define ALLOWED_IMBALANCE      1

#define STACK_PUSH(r)   (stack[stack_size++] = (r))
#define STACK_TOP()     (stack[stack_size-1])
#define STACK_POP()     (stack[--stack_size])


static ALWAYS_INLINE struct bintree_node *
obj_to_bintree_node(void *obj, ptrdiff_t offset)
{
   return obj ? (struct bintree_node *)((char*)obj + offset) : NULL;
}

static ALWAYS_INLINE void *
bintree_node_to_obj(struct bintree_node *node, ptrdiff_t offset)
{
   return node ? (void *)((char*)node - offset) : NULL;
}

#define OBJTN(o) (obj_to_bintree_node((o), bintree_offset))
#define NTOBJ(n) (bintree_node_to_obj((n), bintree_offset))

#define LEFT_OF(obj) ( OBJTN((obj))->left_obj )
#define RIGHT_OF(obj) ( OBJTN((obj))->right_obj )
#define HEIGHT(obj) ((obj) ? OBJTN((obj))->height : -1)

static inline void
update_height(struct bintree_node *node, ptrdiff_t bintree_offset)
{
   node->height = (u16)MAX(HEIGHT(node->left_obj), HEIGHT(node->right_obj)) + 1;
}

#define UPDATE_HEIGHT(n) update_height((n), bintree_offset)


/*
 * Rotate the left child of *obj_ref [which is called `n`] clock-wise
 *
 *         (n)                  (nl)
 *         /  \                 /  \
 *       (nl) (nr)    =>    (nll)  (n)
 *       /  \                      /  \
 *    (nll) (nlr)               (nlr) (nr)
 */

void rotate_left_child(void **obj_ref, ptrdiff_t bintree_offset)
{
   ASSERT(obj_ref != NULL);
   ASSERT(*obj_ref != NULL);

   struct bintree_node *orig_node = OBJTN(*obj_ref);
   ASSERT(orig_node->left_obj != NULL);

   struct bintree_node *orig_left_child = OBJTN(orig_node->left_obj);
   *obj_ref = orig_node->left_obj;
   orig_node->left_obj = orig_left_child->right_obj;
   OBJTN(*obj_ref)->right_obj = NTOBJ(orig_node);

   UPDATE_HEIGHT(orig_node);
   UPDATE_HEIGHT(orig_left_child);
}

/*
 * Rotate the right child of *obj_ref counterclock-wise (symmetric function)
 *
 *       (n)                     (nr)
 *       /  \                    /  \
 *    (nl)  (nr)      =>       (n)  (nrr)
 *          /  \               /  \
 *       (nrl) (nrr)         (nl) (nrl)
 */

void rotate_right_child(void **obj_ref, ptrdiff_t bintree_offset)
{
   ASSERT(obj_ref != NULL);
   ASSERT(*obj_ref != NULL);

   struct bintree_node *orig_node = OBJTN(*obj_ref);
   ASSERT(orig_node->right_obj != NULL);

   struct bintree_node *orig_right_child = OBJTN(orig_node->right_obj);
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

/*
 * This function does the actual node removal and it's called exclusively
 * by bintree_remove_internal() and bintree_remove_ptr_internal() after
 * building the full-path from the tree's root to the node to remove.
 */
static void
bintree_remove_internal_aux(void **root_obj_ref,
                            void ***stack,
                            int stack_size,
                            ptrdiff_t bintree_offset)
{
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
}


void *
bintree_get_first_obj_internal(void *root_obj, ptrdiff_t bintree_offset)
{
   if (!root_obj)
      return NULL;

   while (LEFT_OF(root_obj) != NULL)
      root_obj = LEFT_OF(root_obj);

   return root_obj;
}

void *
bintree_get_last_obj_internal(void *root_obj, ptrdiff_t bintree_offset)
{
   if (!root_obj)
      return NULL;

   while (RIGHT_OF(root_obj) != NULL)
      root_obj = RIGHT_OF(root_obj);

   return root_obj;
}

static ALWAYS_INLINE sptr
bintree_insrem_ptr_cmp(const void *a, const void *b, ptrdiff_t field_off)
{
   const char *f1 = (const char *)a + field_off;
   const char *f2 = (const char *)b + field_off;
   return *(sptr *)f1 - *(sptr *)f2;
}

static ALWAYS_INLINE sptr
bintree_find_ptr_cmp(const void *obj, const void *val, ptrdiff_t field_off)
{
   sptr obj_field_val = *(sptr *)((const char *)obj + field_off);
   return obj_field_val - (sptr)val;
}

/*
 * A powerful macro containing the common code between insert and remove.
 * Briefly, it finds the place where the given `obj_or_value` is (remove)
 * or will be (insert), leaving the node-to-root path in the explicit
 * stack (assumed to exist).
 *
 * This common code has been implemented as a "dirty macro" because a
 * proper C implementation using a function and a dedicated structure
 * for the stack caused an overhead of about 2% due to the necessary
 * indirections (even with -O3 and ALWAYS_INLINE). All the possible
 * alternatives were:
 *
 *    - keeping duplicate code between insert and remove
 *
 *    - sharing the code with a C function + bintree_stack struct and
 *      paying 2% overhead for that luxory
 *
 *    - sharing the code with a macro getting 0% overhead but living
 *      with the potential problems that macros like that might cause
 *      when the code is changed often enough
 */
#define AVL_BUILD_PATH_TO_OBJ()                                        \
   do {                                                                \
      ASSERT(root_obj_ref != NULL);                                    \
      STACK_PUSH(root_obj_ref);                                        \
                                                                       \
      while (*STACK_TOP()) {                                           \
                                                                       \
         sptr c;                                                       \
         void **obj_ref = STACK_TOP();                                 \
         struct bintree_node *node = OBJTN(*obj_ref);                  \
                                                                       \
         if (!(c = CMP(*obj_ref, obj_or_value)))                       \
            break;                                                     \
                                                                       \
         STACK_PUSH(c < 0 ? &node->right_obj : &node->left_obj);       \
      }                                                                \
   } while (0)


/* First, instantiate the generic find, insert and remove functions */
#define BINTREE_PTR_FUNCS 0
#include "avl_find.c.h"
#include "avl_insert.c.h"
#include "avl_remove.c.h"
#undef BINTREE_PTR_FUNCS

/* Then, instantiate the specialized versions of those functions */
#define BINTREE_PTR_FUNCS 1
#include "avl_find.c.h"
#include "avl_insert.c.h"
#include "avl_remove.c.h"
#undef BINTREE_PTR_FUNCS

#include <tilck/common/norec.h>

int
bintree_in_order_visit_internal(void *obj,
                                bintree_visit_cb visit_cb,
                                void *visit_cb_arg,
                                ptrdiff_t bintree_offset,
                                bool reverse)
{
   int r;

   if (!obj)
      return 0;

   CREATE_SHADOW_STACK(MAX_TREE_HEIGHT, 1);
   SIMULATE_CALL1(obj);

   NOREC_LOOP_BEGIN
   {
      obj = LOAD_ARG_FROM_STACK(1, void *);
      void *left_obj = LIKELY(!reverse) ? LEFT_OF(obj) : RIGHT_OF(obj);
      void *right_obj = LIKELY(!reverse) ? RIGHT_OF(obj) : LEFT_OF(obj);

      HANDLE_SIMULATED_RETURN();

      if (left_obj)
         SIMULATE_CALL1(left_obj);

      if ((r = visit_cb(obj, visit_cb_arg)))
         return r;

      if (right_obj)
         SIMULATE_CALL1(right_obj);

      SIMULATE_RETURN_NULL();
   }
   NOREC_LOOP_END
   return 0;
}

/* Re-define STACK_VAR and STACK_SIZE_VAR in order to use them from `ctx` */
#undef   STACK_VAR
#undef   STACK_SIZE_VAR
#define  STACK_VAR       ctx->stack
#define  STACK_SIZE_VAR  ctx->stack_size
#include <tilck/common/norec.h>

void
bintree_in_order_visit_start_internal(struct bintree_walk_ctx *ctx,
                                      void *obj,
                                      ptrdiff_t bintree_offset,
                                      bool reverse)
{
   *ctx = (struct bintree_walk_ctx) {
      .bintree_offset = bintree_offset,
      .obj = obj,
      .reverse = reverse,
      .next_called = false
   };

   INIT_SHADOW_STACK();
}

void *
bintree_in_order_visit_next(struct bintree_walk_ctx *ctx)
{
   /*
    * This declaration is necessary to make the LEFT_OF() and RIGHT_OF() macros
    * to work. NOTE: in *no* case this function might have stack variables other
    * than just aliases of variables taken from `ctx`. That's because it has to
    * support our simple yield mechanism: all of its state *must be* in `ctx`.
    */
   const ptrdiff_t bintree_offset = ctx->bintree_offset;

   if (UNLIKELY(!ctx->next_called)) {

      /*
       * When this function is called for the time for a given context, an
       * initial "call" has to be made in order to create the first frame in our
       * explicit stack.
       */
      ctx->next_called = true;

      if (LIKELY(ctx->obj != NULL))
         SIMULATE_CALL1(ctx->obj);
   }

   NOREC_LOOP_BEGIN
   {
      void *obj = LOAD_ARG_FROM_STACK(1, void *);
      void *left_obj = LIKELY(!ctx->reverse) ? LEFT_OF(obj) : RIGHT_OF(obj);
      void *right_obj = LIKELY(!ctx->reverse) ? RIGHT_OF(obj) : LEFT_OF(obj);

      HANDLE_SIMULATED_RETURN();

      if (left_obj)
         SIMULATE_CALL1(left_obj);

      SIMULATE_YIELD(obj);

      if (right_obj)
         SIMULATE_CALL1(right_obj);

      SIMULATE_RETURN_NULL();
   }
   NOREC_LOOP_END
   return NULL;
}
