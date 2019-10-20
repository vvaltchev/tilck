/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#define MAX_TREE_HEIGHT       32

struct bintree_node {
   void *left_obj;   // pointer to the left container obj
   void *right_obj;  // pointer to the right container obj
   u16 height;
};

static inline void bintree_node_init(struct bintree_node *node)
{
   bzero(node, sizeof(struct bintree_node));
}

#include <tilck/common/norec.h>

typedef struct {

   DECLARE_SHADOW_STACK(MAX_TREE_HEIGHT, 1)
   ptrdiff_t bintree_offset;
   void *obj;
   bool reverse;
   bool next_called;

} bintree_walk_ctx;

#undef STACK_VAR
#undef STACK_SIZE_VAR

/*
 * bintree_find_internal() returns true it was actually able to insert the
 * object and false it case an object with the same 'value' (cmp(a,b) == 0) was
 * already in the tree.
 */
bool
bintree_insert_internal(void **root_obj_ref,
                        void *obj,
                        cmpfun_ptr cmp, // cmp(*root_obj_ref, obj)
                        ptrdiff_t bintree_offset);

/*
 * bintree_find_internal() returns an obj* or NULL.
 *
 * NOTE: typeof(root_obj) usually != typeof(value_ptr)
 * While bintree_insert_internal() uses a compare function for comparing
 * pointer of objects [containing at offset +bintree_offset a bintree_node],
 * this function uses a compare function that compares an object* with a
 * generic pointer to a value. That different approach is necessary to allow
 * find() to be called without having to create a whole object for the value.
 * I.e. usually the value used to compare objects is just an integer inside
 * an object: in that case value_ptr is just a pointer to an integer and the
 * compare function needs to be able to compare those two different pointers.
 */
void *
bintree_find_internal(void *root_obj,
                      const void *value_ptr,
                      cmpfun_ptr objval_cmpfun,   // cmp(root_obj, value_ptr)
                      ptrdiff_t bintree_offset);


/*
 * returns a pointer to the removed object (if found) or NULL.
 */
void *
bintree_remove_internal(void **root_obj_ref,
                        void *value_ptr,
                        cmpfun_ptr objval_cmpfun, //cmp(*root_obj_ref,value_ptr)
                        ptrdiff_t bintree_offset);


typedef int (*bintree_visit_cb) (void *obj, void *arg);

int
bintree_in_order_visit_internal(void *root_obj,
                                bintree_visit_cb visit_cb,
                                void *visit_cb_arg,
                                ptrdiff_t bintree_offset,
                                bool reverse);

void
bintree_in_order_visit_start_internal(bintree_walk_ctx *ctx,
                                      void *obj,
                                      ptrdiff_t bintree_offset,
                                      bool reverse);
void *
bintree_in_order_visit_next(bintree_walk_ctx *ctx);

void *
bintree_get_first_obj_internal(void *root_obj, ptrdiff_t bintree_offset);

void *
bintree_get_last_obj_internal(void *root_obj, ptrdiff_t bintree_offset);

bool
bintree_insert_ptr_internal(void **root_obj_ref,
                            void *obj,
                            ptrdiff_t bintree_offset,
                            ptrdiff_t field_off);
void *
bintree_find_ptr_internal(void *root_obj,
                          const void *value_ptr,
                          ptrdiff_t bintree_offset,
                          ptrdiff_t field_off);
void *
bintree_remove_ptr_internal(void **root_obj_ref,
                            void *value_ptr,
                            ptrdiff_t bintree_offset,
                            ptrdiff_t field_off);


#define bintree_insert(rootref, obj, cmpfun, struct_type, elem_name)          \
   bintree_insert_internal((void **)(rootref), (void*)obj, cmpfun,            \
                           OFFSET_OF(struct_type, elem_name))

#define bintree_insert_ptr(rootref, obj, struct_type, elem_name, field_name)  \
   bintree_insert_ptr_internal((void **)(rootref), (void*)obj,                \
                               OFFSET_OF(struct_type, elem_name),             \
                               OFFSET_OF(struct_type, field_name))

/*
 * Find the object with key `value` according to objval_cmpfun.
 * The `value` will be passes as it is to `objval_cmpfun`, which will see it
 * as a const void * pointer. Whether `value` is a pointer to a value or the
 * value itself represented in a pointer, depends entirely on the caller, who
 * controls `objval_cmpfun`. But, it's worth mentioning that, typically, `value`
 * here is a pointer to a value (e.g. pointer to char array), not the value
 * itself. In case the value itself can fit in a pointer, usually is better to
 * use bintree_find_ptr() because it's faster. But WARNING: bintree_find_ptr()
 * treats `value` as a value, not as a pointer to a value and that's hard-coded.
 */
#define bintree_find(root_obj, value, objval_cmpfun, struct_type, elem_name)  \
   bintree_find_internal((void*)(root_obj),                                   \
                         (value), (objval_cmpfun),                            \
                         OFFSET_OF(struct_type, elem_name))

/*
 * Find the object with key `value`, where value is a pointer-sized integer.
 * The comparison function is hard-coded and it just compares the given field
 * (`field_name`) of each obj to the given value.
 */
#define bintree_find_ptr(root_obj, value, struct_type, elem_name, field_name) \
   bintree_find_ptr_internal((void*)(root_obj),                               \
                             TO_PTR(value),                                   \
                             OFFSET_OF(struct_type, elem_name),               \
                             OFFSET_OF(struct_type, field_name))

#define bintree_remove(rootref, value, objval_cmpfun, struct_type, elem_name) \
   bintree_remove_internal((void**)(rootref),                                 \
                           (value), (objval_cmpfun),                          \
                           OFFSET_OF(struct_type, elem_name))

#define bintree_remove_ptr(rootref, value, struct_type, elem_name, field_name) \
   bintree_remove_ptr_internal((void**)(rootref),                              \
                           (value),                                            \
                           OFFSET_OF(struct_type, elem_name),                  \
                           OFFSET_OF(struct_type, field_name))

#define bintree_in_order_visit(root_obj, cb, cb_arg, struct_type, elem_name)  \
   bintree_in_order_visit_internal((void *)(root_obj),                        \
                                   (cb), (cb_arg),                            \
                                   OFFSET_OF(struct_type, elem_name),         \
                                   false)

#define bintree_in_rorder_visit(root_obj, cb, cb_arg, struct_type, elem_name) \
   bintree_in_order_visit_internal((void *)(root_obj),                        \
                                   (cb), (cb_arg),                            \
                                   OFFSET_OF(struct_type, elem_name),         \
                                   true)

#define bintree_get_first_obj(root_obj, struct_type, elem_name)               \
   bintree_get_first_obj_internal((void *)(root_obj),                         \
                                  OFFSET_OF(struct_type, elem_name))

#define bintree_get_last_obj(root_obj, struct_type, elem_name)               \
   bintree_get_last_obj_internal((void *)(root_obj),                         \
                                 OFFSET_OF(struct_type, elem_name))

#define bintree_in_order_visit_start(ctx, obj, struct_type, elem_name, rev)  \
   bintree_in_order_visit_start_internal(ctx,                                \
                                         (void*)(obj),                       \
                                         OFFSET_OF(struct_type, elem_name),  \
                                         rev)
