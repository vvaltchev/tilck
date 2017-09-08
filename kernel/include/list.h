
#pragma once
#include <common_defs.h>

struct list_node {
   struct list_node *next;
   struct list_node *prev;
};

typedef struct list_node list_node;

#define list_node_make(name) { &(name), &(name) }

static inline void list_node_init(list_node *list)
{
   list->next = list;
   list->prev = list;
}

static inline bool list_is_empty(list_node *list) {
   return list->next == list && list->prev == list;
}

static inline void list_add_after(list_node *curr, list_node *elem)
{
   list_node *curr_next = curr->next;
   curr->next = elem;
   elem->next = curr_next;
   elem->prev = curr;
   curr_next->prev = elem;
}

// Add 'elem' before 'curr'. If 'curr' is the root list_node, adds on tail.
static inline void list_add_before(list_node *curr, list_node *elem)
{
   list_node *curr_prev = curr->prev;
   curr->prev = elem;
   elem->prev = curr_prev;
   elem->next = curr;
   curr_prev->next = elem;
}

static inline void list_remove(list_node *elem)
{
   elem->prev->next = elem->next;
   elem->next->prev = elem->prev;
}

#define list_entry(list_ptr, struct_type, list_member_name) \
   CONTAINER_OF(list_ptr, struct_type, list_member_name)

#define list_curr_entry(struct_type, list_member_name) \
   list_entry(_pos, struct_type, list_member_name)

#define list_first_entry(list_ptr, type, member) \
   list_entry((list_ptr)->next, type, member)

#define list_last_entry(list_ptr, type, member) \
   list_entry((list_ptr)->prev, type, member)

// Here 'pos' is a struct *, contaning a list_node 'member'

#define list_next_entry(pos, list_mem_name) \
   list_entry((pos)->list_mem_name.next, typeof(*(pos)), list_mem_name)

#define list_prev_entry(pos, list_mem_name) \
   list_entry((pos)->list_mem_name.prev, typeof(*(pos)),list_mem_name)

#define list_for_each_entry(pos, list_node, member)                 \
   for (pos = list_first_entry(list_node, typeof(*pos), member);    \
        &pos->member != (list_node);                                \
        pos = list_next_entry(pos, member))

#define list_for_each_entry_reverse(pos, list_node, member)         \
   for (pos = list_last_entry(list_node, typeof(*pos), member);     \
        &pos->member != (list_node);                                \
        pos = list_prev_entry(pos, member))

// Here 'pos' is a list_node* pointer.

#define list_for_each(pos, head) \
   for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_prev(pos, head) \
   for (pos = (head)->prev; pos != (head); pos = pos->prev)

#define list_for_each2(head)        \
   list_node *_pos;                 \
   list_for_each(_pos, head)

#define list_for_each_prev2(head)   \
   list_node *_pos;                 \
   list_for_each_prev(_pos, head)
