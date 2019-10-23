/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct list_node {
   struct list_node *next;
   struct list_node *prev;
};

struct list {
   struct list_node *first;
   struct list_node *last;
};

STATIC_ASSERT(sizeof(struct list) == sizeof(struct list_node));
STATIC_ASSERT(alignof(struct list) == alignof(struct list_node));

#define make_list(name) {                                       \
   (struct list_node *)&(name),                                 \
   (struct list_node *)&(name)                                  \
}

#define make_list_node(name) { &(name), &(name) }

static inline void list_node_init(struct list_node *n)
{
   n->next = n;
   n->prev = n;
}

static inline bool list_node_is_null(struct list_node *n) {
   return !n->next && !n->prev;
}

static inline bool list_node_is_empty(struct list_node *n) {
   return n->next == n && n->prev == n;
}

static inline void list_init(struct list *n) {
   list_node_init((struct list_node *)n);
}

static inline bool list_is_null(struct list *n) {
   return list_node_is_null((struct list_node *) n);
}

static inline bool list_is_empty(struct list *n) {
   return list_node_is_empty((struct list_node *) n);
}

static inline bool list_is_node_in_list(struct list_node *node) {
   return !list_node_is_empty(node) &&
          node->prev->next == node &&
          node->next->prev == node;
}

static inline void
list_add_after(struct list_node *curr, struct list_node *elem)
{
   struct list_node *curr_next = curr->next;
   curr->next = elem;
   elem->next = curr_next;
   elem->prev = curr;
   curr_next->prev = elem;
}

static inline void
list_add_before(struct list_node *curr, struct list_node *elem)
{
   list_add_after(curr->prev, elem);
}

static inline void list_add_tail(struct list *l, struct list_node *elem)
{
   list_add_before((struct list_node *)l, elem);
}

static inline void list_add_head(struct list *l, struct list_node *elem)
{
   list_add_after((struct list_node *)l, elem);
}

static inline void list_remove(struct list_node *elem)
{
   elem->prev->next = elem->next;
   elem->next->prev = elem->prev;
}

#define list_to_obj(list_ptr, struct_type, list_member_name) \
   CONTAINER_OF(list_ptr, struct_type, list_member_name)

#define list_first_obj(list_root_ptr, type, member) \
   list_to_obj((list_root_ptr)->first, type, member)

#define list_last_obj(list_root_ptr, type, member) \
   list_to_obj((list_root_ptr)->last, type, member)

// Here 'pos' is an object (struct *), containing a struct list_node 'member'

#define list_next_obj(pos, list_mem_name) \
   list_to_obj((pos)->list_mem_name.next, typeof(*(pos)), list_mem_name)

#define list_prev_obj(pos, list_mem_name) \
   list_to_obj((pos)->list_mem_name.prev, typeof(*(pos)), list_mem_name)

// Here 'tp' is a temporary variable having the same type of 'pos'.

#define list_for_each(pos, tp, list_ptr, member)                     \
   for (pos = list_first_obj(list_ptr, typeof(*pos), member),        \
        tp = list_next_obj(pos, member);                             \
        &pos->member != (struct list_node *)(list_ptr);                     \
        pos = tp, tp = list_next_obj(tp, member))

#define list_for_each_ro(pos, list_ptr, member)                      \
   for (pos = list_first_obj(list_ptr, typeof(*pos), member);        \
        &pos->member != (struct list_node *)(list_ptr);                     \
        pos = list_next_obj(pos, member))

/* Same as list_for_each_ro(), but the orig. value of `pos` is kept */
#define list_for_each_ro_kp(pos, list_ptr, member)                   \
   for (; &pos->member != (struct list_node *)(list_ptr);                   \
        pos = list_next_obj(pos, member))

#define list_for_each_reverse(pos, tp, list_ptr, member)             \
   for (pos = list_last_obj(list_ptr, typeof(*pos), member),         \
        tp = list_prev_obj(pos, member);                             \
        &pos->member != (list_ptr);                                  \
        pos = tp, tp = list_prev_obj(tp, member))
