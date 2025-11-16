/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/string_util.h>
#include <tilck/kernel/bintree.h>

#define ALLOWED_IMBALANCE      1

#define STACK_PUSH(r)   (stack[stack_size++] = (r))
#define STACK_TOP()     (stack[stack_size-1])
#define STACK_POP()     (stack[--stack_size])


static ALWAYS_INLINE struct bintree_node *
obj_to_bintree_node(void *obj, long offset)
{
   return obj ? (struct bintree_node *)((void *)obj + offset) : NULL;
}

static ALWAYS_INLINE void *
bintree_node_to_obj(struct bintree_node *node, long offset)
{
   return node ? ((void *)node - offset) : NULL;
}

#define OBJTN(o) (obj_to_bintree_node((o), bintree_offset))
#define NTOBJ(n) (bintree_node_to_obj((n), bintree_offset))

#define LEFT_OF(obj) ( OBJTN((obj))->left_obj )
#define RIGHT_OF(obj) ( OBJTN((obj))->right_obj )
#define HEIGHT(obj) ((obj) ? OBJTN((obj))->height : -1)

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
#define AVL_BUILD_PATH_TO_OBJ()                                        \
   do {                                                                \
      ASSERT(root_obj_ref != NULL);                                    \
      STACK_PUSH(root_obj_ref);                                        \
                                                                       \
      while (*STACK_TOP()) {                                           \
                                                                       \
         long c;                                                       \
         void **obj_ref = STACK_TOP();                                 \
         struct bintree_node *node = OBJTN(*obj_ref);                  \
                                                                       \
         if (!(c = CMP(*obj_ref, obj_or_value)))                       \
            break;                                                     \
                                                                       \
         STACK_PUSH(c < 0 ? &node->right_obj : &node->left_obj);       \
      }                                                                \
   } while (0)

void rotate_left_child(void **obj_ref, long bintree_offset);
void rotate_right_child(void **obj_ref, long bintree_offset);
static void balance(void **obj_ref, long bintree_offset);

#define ROTATE_CW_LEFT_CHILD(obj) (rotate_left_child((obj), bintree_offset))
#define ROTATE_CCW_RIGHT_CHILD(obj) (rotate_right_child((obj), bintree_offset))
#define BALANCE(obj) (balance((obj), bintree_offset))

static void
bintree_remove_internal_aux(void **root_obj_ref,
                            void ***stack,
                            int stack_size,
                            long bintree_offset);

