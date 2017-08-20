
#pragma once
#include <common_defs.h>

// Standard compare function signature.
typedef int (*cmpfun_ptr)(const void *a, const void *b);

typedef struct bintree_node bintree_node;

struct bintree_node {
   bintree_node *left;
   bintree_node *right;
   int height;
};

#define bintree_entry(ptr, struct_type, bintree_node_elem_name) \
   CONTAINER_OF(ptr, struct_type, bintree_node_elem_name)

bool bintree_insert(bintree_node *root, bintree_node *node, cmpfun_ptr f);
bool bintree_remove(bintree_node *root, bintree_node *node, cmpfun_ptr f);


bintree_node *
bintree_find_internal(bintree_node *root,
                      const void *value,     // pointer to a val to be searched
                      size_t bintree_offset, // offset of the bintree_node elem
                                             // relative to the containter.
                      cmpfun_ptr f);

#define bintree_find_node(root, value_ptr, cmpfun, struct_type, elem_name) \
   bintree_find_internal(root, value_ptr, \
                         OFFSET_OF(struct_type, elem_name), cmpfun)
