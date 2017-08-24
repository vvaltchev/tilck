
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

bool
bintree_insert_internal(void **root_obj_ref,
                        void *obj,
                        cmpfun_ptr cmp,
                        ptrdiff_t bintree_offset);

void *
bintree_find_internal(void *root_obj,
                      const uptr value,
                      cmpfun_objval_ptr cmp,
                      ptrdiff_t bintree_offset);

#define bintree_insert(rootref, obj, cmpfun, struct_type, elem_name)   \
   bintree_insert_internal((void **)(rootref), (void*)obj, cmpfun,     \
                           OFFSET_OF(struct_type, elem_name))

#define bintree_find(root_obj, value, objval_cmpfun, struct_type, elem_name)  \
   bintree_find_internal((void*)(root_obj),                                   \
                         (value), (objval_cmpfun),                            \
                         OFFSET_OF(struct_type, elem_name))
