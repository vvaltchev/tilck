/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/string_util.h>
#include <tilck/kernel/bintree.h>

#define ALLOWED_IMBALANCE      1

#define STACK_PUSH(r)   (stack[stack_size++] = (r))
#define STACK_TOP()     (stack[stack_size-1])
#define STACK_POP()     (stack[--stack_size])


/*
 * Default form: plain pointer arithmetic with no NULL check. The
 * NULL-checking variants below are the explicit-safety exception,
 * not the rule, because at -O3 the compiler often emits the check
 * as a dead `test` + zero-displacement `je` that wastes fetch/decode
 * bandwidth and a BTB slot. Use the unchecked form in hot paths
 * where the invariant is already established (ASSERT, prior check,
 * or an enclosing loop guard).
 */
static ALWAYS_INLINE struct bintree_node *
obj_to_bintree_node(void *obj, long offset)
{
   return (struct bintree_node *)((void *)obj + offset);
}

static ALWAYS_INLINE void *
bintree_node_to_obj(struct bintree_node *node, long offset)
{
   return (void *)node - offset;
}

/*
 * "_checked" variants: NULL-safe wrappers, for the rare callers
 * that may legitimately pass NULL (currently just HEIGHT).
 */
static ALWAYS_INLINE struct bintree_node *
obj_to_bintree_node_checked(void *obj, long offset)
{
   return obj ? obj_to_bintree_node(obj, offset) : NULL;
}

static ALWAYS_INLINE void *
bintree_node_to_obj_checked(struct bintree_node *node, long offset)
{
   return node ? bintree_node_to_obj(node, offset) : NULL;
}

#define OBJTN(o) (obj_to_bintree_node_checked((o), bintree_offset))
#define NTOBJ(n) (bintree_node_to_obj_checked((n), bintree_offset))
#define OBJTN_NN(o) (obj_to_bintree_node((o), bintree_offset))
#define NTOBJ_NN(n) (bintree_node_to_obj((n), bintree_offset))

/*
 * LEFT_OF / RIGHT_OF immediately dereference their result, so the
 * argument must be non-NULL by contract -- use the _nn form to skip
 * the redundant NULL check. HEIGHT is the only macro that legitimately
 * takes a possibly-NULL obj (children can be NULL); it does its own
 * check first, then OBJTN_NN.
 */
#define LEFT_OF(obj) ( OBJTN_NN((obj))->left_obj )
#define RIGHT_OF(obj) ( OBJTN_NN((obj))->right_obj )
#define HEIGHT(obj) ((obj) ? OBJTN_NN((obj))->height : -1)

static inline void
update_height(struct bintree_node *node, long bintree_offset)
{
   node->height = (u16)MAX(
      HEIGHT(node->left_obj), HEIGHT(node->right_obj)
   ) + 1;
}

#define UPDATE_HEIGHT(n) update_height((n), bintree_offset)

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
#define AVL_BUILD_PATH_TO_OBJ()                                       \
   do {                                                               \
      ASSERT(root_obj_ref != NULL);                                   \
      STACK_PUSH(root_obj_ref);                                       \
                                                                      \
      while (*STACK_TOP()) {                                          \
                                                                      \
         long c;                                                      \
         void **obj_ref = STACK_TOP();                                \
         struct bintree_node *node = OBJTN_NN(*obj_ref);              \
                                                                      \
         if (!(c = CMP(*obj_ref, obj_or_value)))                      \
            break;                                                    \
                                                                      \
         STACK_PUSH(c < 0 ? &node->right_obj : &node->left_obj);      \
      }                                                               \
   } while (0)

static bool balance(void **obj_ref, long bintree_offset);

#define ROTATE_CW_LEFT_CHILD(obj) (rotate_left_child((obj), bintree_offset))
#define ROTATE_CCW_RIGHT_CHILD(obj) (rotate_right_child((obj), bintree_offset))
#define BALANCE(obj) (balance((obj), bintree_offset))

static void
bintree_remove_internal_aux(void **root_obj_ref,
                            void ***stack,
                            int stack_size,
                            long bintree_offset);

