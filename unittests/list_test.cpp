
#include <iostream>
#include <gtest/gtest.h>

#include <common_defs.h>
#include <list.h>

using namespace std;

typedef struct {

   const char *data;
   list_head list;

} my_struct;


static my_struct *create_elem(const char *data = nullptr) {
   my_struct *e = (my_struct *) malloc(sizeof(my_struct));
   INIT_LIST_HEAD(&e->list);
   e->data = data;
   return e;
}

static void destroy_elem(my_struct *e) {
   free(e);
}


TEST(list_adt, initialization)
{
   list_head list = LIST_HEAD_INIT(list);

   ASSERT_TRUE(list_is_empty(&list));

   ASSERT_TRUE(list.next == &list);
   ASSERT_TRUE(list.prev == &list);

   bzero(&list, sizeof(list));

   ASSERT_TRUE(list.next == 0);
   ASSERT_TRUE(list.prev == 0);

   INIT_LIST_HEAD(&list);
   ASSERT_TRUE(list.next == &list);
   ASSERT_TRUE(list.prev == &list);
}


TEST(list_adt, add)
{
   my_struct *e1 = create_elem("head");
   my_struct *e2 = create_elem("tail");

   ASSERT_TRUE(list_entry(&e1->list, my_struct, list) == e1);
   ASSERT_TRUE(list_entry(&e2->list, my_struct, list) == e2);

   LIST_HEAD(list);
   ASSERT_TRUE(list_is_empty(&list));

   list_add(&list, &e1->list);
   list_add(&e1->list, &e2->list);

   ASSERT_TRUE(e1->list.next == &e2->list);
   ASSERT_TRUE(e1->list.next->next == &list);

   ASSERT_TRUE(e1->list.prev == &list);
   ASSERT_TRUE(e2->list.prev == &e1->list);

   my_struct *e12 = create_elem("mid");
   list_add(&e1->list, &e12->list);

   ASSERT_TRUE(e1->list.next == &e12->list);
   ASSERT_TRUE(e1->list.next->next == &e2->list);

   ASSERT_TRUE(e1->list.prev == &list);
   ASSERT_TRUE(e2->list.prev == &e12->list);

   ASSERT_TRUE(e2->list.next == &list);
   ASSERT_TRUE(list.prev == &e2->list);

   ASSERT_TRUE(list_entry(&e1->list, my_struct, list) == e1);
   ASSERT_TRUE(list_entry(&e2->list, my_struct, list) == e2);
   ASSERT_TRUE(list_entry(&e12->list, my_struct, list) == e12);

   destroy_elem(e1);
   destroy_elem(e2);
   destroy_elem(e12);
}

TEST(list_adt, add_tail)
{
   my_struct *e1 = create_elem("head");
   my_struct *e2 = create_elem("tail");

   ASSERT_TRUE(list_entry(&e1->list, my_struct, list) == e1);
   ASSERT_TRUE(list_entry(&e2->list, my_struct, list) == e2);

   LIST_HEAD(list);
   ASSERT_TRUE(list_is_empty(&list));

   list_add(&list, &e1->list);
   list_add(&e1->list, &e2->list);

   my_struct *ne = create_elem("new tail");
   list_add_tail(&list, &ne->list);

   ASSERT_TRUE(list.prev == &ne->list);
   ASSERT_TRUE(ne->list.next == &list);
   ASSERT_TRUE(e2->list.next == &ne->list);
   ASSERT_TRUE(ne->list.prev == &e2->list);

   destroy_elem(e1);
   destroy_elem(e2);
   destroy_elem(ne);
}
