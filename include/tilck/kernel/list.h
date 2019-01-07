/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct list_node {
   struct list_node *next;
   struct list_node *prev;
};

struct list {
   struct list_node *next;
   struct list_node *prev;
};

typedef struct list list;
typedef struct list_node list_node;

#define make_list(name) { (list_node *)&(name), (list_node *)&(name) }
#define make_list_node(name) { &(name), &(name) }

static inline void list_node_init(list_node *n)
{
   n->next = n;
   n->prev = n;
}

static inline void list_init(list *n) {
   list_node_init((list_node *)n);
}

static inline bool list_is_null(list *n) {
   return !n->next && !n->prev;
}

static inline bool list_is_empty(list *n) {
   return n->next == (list_node *)n && n->prev == (list_node *)n;
}

static inline bool list_node_is_null(list_node *n) {
   return list_is_null((list *) n);
}

static inline bool list_node_is_empty(list_node *n) {
   return list_is_empty((list *) n);
}

static inline bool list_is_node_in_list(list_node *node) {
   return !list_is_empty((list *)node) &&
          node->prev->next == node &&
          node->next->prev == node;
}

static inline void list_add_after(list_node *curr, list_node *elem)
{
   list_node *curr_next = curr->next;
   curr->next = elem;
   elem->next = curr_next;
   elem->prev = curr;
   curr_next->prev = elem;
}

static inline void list_add_before(list_node *curr, list_node *elem)
{
   list_add_after(curr->prev, elem);
}

static inline void list_add_tail(list *l, list_node *elem)
{
   list_add_before((list_node *)l, elem);
}

static inline void list_add_head(list *l, list_node *elem)
{
   list_add_after((list_node *)l, elem);
}

static inline void list_remove(list_node *elem)
{
   elem->prev->next = elem->next;
   elem->next->prev = elem->prev;
}

#define list_to_obj(list_ptr, struct_type, list_member_name) \
   CONTAINER_OF(list_ptr, struct_type, list_member_name)

#define list_first_obj(list_root_ptr, type, member) \
   list_to_obj((list_root_ptr)->next, type, member)

#define list_last_obj(list_root_ptr, type, member) \
   list_to_obj((list_root_ptr)->prev, type, member)

// Here 'pos' is an object (struct *), containing a list_node 'member'

#define list_next_obj(pos, list_mem_name) \
   list_to_obj((pos)->list_mem_name.next, typeof(*(pos)), list_mem_name)

#define list_prev_obj(pos, list_mem_name) \
   list_to_obj((pos)->list_mem_name.prev, typeof(*(pos)), list_mem_name)

// Here 'tp' is a temporary variable having the same type of 'pos'.

#define list_for_each(pos, tp, node, member)                     \
   for (pos = list_first_obj(node, typeof(*pos), member),        \
        tp = list_next_obj(pos, member);                         \
        &pos->member != (list_node *)(node);                     \
        pos = tp, tp = list_next_obj(tp, member))

#define list_for_each_reverse(pos, tp, node, member)             \
   for (pos = list_last_obj(node, typeof(*pos), member),         \
        tp = list_prev_obj(pos, member);                         \
        &pos->member != (node);                                  \
        pos = tp, tp = list_prev_obj(tp, member))
