
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

static inline void list_add_tail(list_head *list, list_head *elem)
{
   NOT_REACHED();
}

static inline void list_remove(list_head *elem)
{
   NOT_REACHED();
}

#define LIST_ENTRY(list_ptr, struct_type, list_member_name) \
   CONTAINER_OF(list_ptr, struct_type, list_member_name)
