/* SPDX-License-Identifier: BSD-2-Clause */

#include <iostream>
#include <gtest/gtest.h>

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/list.h>

using namespace std;

struct my_struct {

   const char *data;
   list_node node;

   my_struct(const char *data) : data(data) { }
};

using tvec = vector<const char *>;

void check_list_elems(list& list, const tvec& exp)
{
   size_t i = 0;
   my_struct *pos, *temp;

   list_for_each(pos, temp, &list, node) {

      if (i >= exp.size()) {
         FAIL() << "List is longer than the expected vector";
      }

      if (pos->data != exp[i]) {
         FAIL() << "List elem[" << i << "] = '"
                << pos->data << "' != " << exp[i] << " (expected)";
      }

      i++;
   }
}

TEST(list_adt, initialization)
{
   list list = make_list(list);

   ASSERT_TRUE(list_is_empty(&list));

   ASSERT_TRUE(list.first == (list_node*)&list);
   ASSERT_TRUE(list.last == (list_node*)&list);

   bzero(&list, sizeof(list));

   list_init(&list);
   ASSERT_TRUE(list.first == (list_node*)&list);
   ASSERT_TRUE(list.last == (list_node*)&list);

   check_list_elems(list, tvec{});
}


TEST(list_adt, add)
{
   my_struct e1("head");
   my_struct e2("tail");

   ASSERT_TRUE(list_to_obj(&e1.node, my_struct, node) == &e1);
   ASSERT_TRUE(list_to_obj(&e2.node, my_struct, node) == &e2);

   list list;
   list_init(&list);
   ASSERT_TRUE(list_is_empty(&list));

   list_add_after((list_node *)&list, &e1.node);
   list_add_after(&e1.node, &e2.node);

   ASSERT_TRUE(e1.node.next == &e2.node);
   ASSERT_TRUE(e1.node.next->next == (list_node *)&list);

   ASSERT_TRUE(e1.node.prev == (list_node *)&list);
   ASSERT_TRUE(e2.node.prev == &e1.node);

   check_list_elems(list, tvec{"head", "tail"});

   my_struct e12("mid");
   list_add_after(&e1.node, &e12.node);

   ASSERT_TRUE(e1.node.next == &e12.node);
   ASSERT_TRUE(e1.node.next->next == &e2.node);

   ASSERT_TRUE(e1.node.prev == (list_node *)&list);
   ASSERT_TRUE(e2.node.prev == &e12.node);

   ASSERT_TRUE(e2.node.next == (list_node *)&list);
   ASSERT_TRUE(list.last == &e2.node);

   ASSERT_TRUE(list_to_obj(&e1.node, my_struct, node) == &e1);
   ASSERT_TRUE(list_to_obj(&e2.node, my_struct, node) == &e2);
   ASSERT_TRUE(list_to_obj(&e12.node, my_struct, node) == &e12);

   check_list_elems(list, tvec{"head", "mid", "tail"});
}

TEST(list_adt, add_tail)
{
   my_struct e1("head");
   my_struct e2("tail");

   ASSERT_TRUE(list_to_obj(&e1.node, my_struct, node) == &e1);
   ASSERT_TRUE(list_to_obj(&e2.node, my_struct, node) == &e2);

   list list;
   list_init(&list);

   ASSERT_TRUE(list_is_empty(&list));

   list_add_after((list_node *)&list, &e1.node);
   list_add_after(&e1.node, &e2.node);

   check_list_elems(list, tvec{"head", "tail"});

   my_struct ne("new tail");
   list_add_tail(&list, &ne.node);

   ASSERT_TRUE(list.last == &ne.node);
   ASSERT_TRUE(ne.node.next == (list_node *)&list);
   ASSERT_TRUE(e2.node.next == &ne.node);
   ASSERT_TRUE(ne.node.prev == &e2.node);

   check_list_elems(list, tvec{"head", "tail", "new tail"});
}

TEST(list_adt, remove_elem)
{
   list list;
   list_init(&list);

   my_struct e1("e1"), e2("e2"), e3("e3");

   list_add_tail(&list, &e1.node);
   list_add_tail(&list, &e2.node);
   list_add_tail(&list, &e3.node);
   check_list_elems(list, tvec{"e1", "e2", "e3"});

   ASSERT_TRUE(list_is_node_in_list(&e2.node));

   list_remove(&e2.node);
   check_list_elems(list, tvec{"e1", "e3"});
   ASSERT_FALSE(list_is_node_in_list(&e2.node));

   // now, remove e2 again and check that nothing really happened
   list_remove(&e2.node);
   check_list_elems(list, tvec{"e1", "e3"});
   ASSERT_FALSE(list_is_node_in_list(&e2.node));

   list_node a_node;
   list_node_init(&a_node);
   ASSERT_FALSE(list_is_node_in_list(&a_node));
}

TEST(list_adt, add_head)
{
   list list;
   list_init(&list);

   my_struct e1("e1"), e2("e2"), e3("e3");

   list_add_head(&list, &e1.node);
   list_add_head(&list, &e2.node);
   list_add_head(&list, &e3.node);

   check_list_elems(list, tvec{"e3", "e2", "e1"});
}
