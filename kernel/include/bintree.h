
#pragma once
#include <common_defs.h>
#include <string_util.h>

typedef struct bintree_node bintree_node;

struct bintree_node {
   void *left_obj;   // pointer to the left container obj
   void *right_obj;  // pointer to the right container obj
   u16 height;
};

static inline void bintree_node_init(bintree_node *node)
{
   bzero(node, sizeof(bintree_node));
}

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
                      void *value_ptr,
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

#define bintree_insert(rootref, obj, cmpfun, struct_type, elem_name)   \
   bintree_insert_internal((void **)(rootref), (void*)obj, cmpfun,     \
                           OFFSET_OF(struct_type, elem_name))

#define bintree_find(root_obj, value, objval_cmpfun, struct_type, elem_name)  \
   bintree_find_internal((void*)(root_obj),                                   \
                         (value), (objval_cmpfun),                            \
                         OFFSET_OF(struct_type, elem_name))

#define bintree_remove(rootref, value, objval_cmpfun, struct_type, elem_name) \
   bintree_remove_internal((void**)(rootref),                                 \
                           (value), (objval_cmpfun),                          \
                           OFFSET_OF(struct_type, elem_name))
