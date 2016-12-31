
#pragma once
#include <common_defs.h>

/*
 * Doubly-linked list, like the one in the Linux kernel.
 * Some functions/types/macros have been copy-pasted, other re-written.
 * See Kernel's include/linux/list.h file for better understanding the
 * differences and the similarities.
 */

struct list_head {
   struct list_head *next;
   struct list_head *prev;
};

typedef struct list_head list_head;

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(list_head *list)
{
   list->next = list;
   list->prev = list;
}

static inline bool list_is_empty(list_head *list) {
   return list->next == list && list->prev == list;
}

static inline void list_add(list_head *curr, list_head *elem)
{
   list_head *curr_next = curr->next;
   curr->next = elem;
   elem->next = curr_next;
   elem->prev = curr;
   curr_next->prev = elem;
}

static inline void list_add_tail(list_head *curr, list_head *elem)
{
   list_head *curr_prev = curr->prev;
   curr->prev = elem;
   elem->prev = curr_prev;
   elem->next = curr;
   curr_prev->next = elem;
}

static inline void list_remove(list_head *elem)
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

// Here 'pos' is a struct *, contaning a list_head 'member'

#define list_next_entry(pos, list_mem_name) \
   list_entry((pos)->list_mem_name.next, typeof(*(pos)), list_mem_name)

#define list_prev_entry(pos, list_mem_name) \
   list_entry((pos)->list_mem_name.prev, typeof(*(pos)),list_mem_name)

#define list_for_each_entry(pos, list_head, member)                 \
   for (pos = list_first_entry(list_head, typeof(*pos), member);    \
        &pos->member != (list_head);                                \
        pos = list_next_entry(pos, member))

#define list_for_each_entry_reverse(pos, list_head, member)         \
   for (pos = list_last_entry(list_head, typeof(*pos), member);     \
        &pos->member != (list_head);                                \
        pos = list_prev_entry(pos, member))

// Here 'pos' is a list_head* pointer.

#define list_for_each(pos, head) \
   for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_prev(pos, head) \
   for (pos = (head)->prev; pos != (head); pos = pos->prev)

#define list_for_each2(head)        \
   list_head *_pos;                 \
   list_for_each(_pos, head)

#define list_for_each_prev2(head)   \
   list_head *_pos;                 \
   list_for_each_prev(_pos, head)
