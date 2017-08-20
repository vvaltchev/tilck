
#pragma once
#include <common_defs.h>
#include <string_util.h>

typedef struct bintree_node bintree_node;

struct bintree_node {
   void *left;   // pointer to the left container obj
   void *right;  // pointer to the right container obj
   u16 height;
};

#define bintree_entry(ptr, struct_type, bintree_node_elem_name) \
   CONTAINER_OF(ptr, struct_type, bintree_node_elem_name)

static inline void bintree_node_init(bintree_node *node)
{
   bzero(node, sizeof(bintree_node));
}

bool
bintree_insert_internal(void **root,
                        void *obj,
                        cmpfun_ptr f,
                        ptrdiff_t bintree_offset);


#define bintree_insert(rootref, obj, cmpfun, struct_type, elem_name) \
   bintree_insert_internal((void **)rootref, (void*)obj, cmpfun,     \
                           OFFSET_OF(struct_type, elem_name))
