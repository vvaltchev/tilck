/* SPDX-License-Identifier: BSD-2-Clause */

#include <iostream>
#include <gtest/gtest.h>

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/list.h>

using namespace std;

struct my_struct {

   const char *data;
   struct list_node node;

   my_struct(const char *d) : data(d) {
      list_node_init(&node);
   }
};

extern "C" {

   my_struct *debug_list_test_get_struct(struct list_node *n) {
      my_struct *s = list_to_obj(n, my_struct, node);
      return s;
   }

   const char *debug_list_test_dump_data(struct list_node *n) {
      return debug_list_test_get_struct(n)->data;
   }

   void debug_list_test_dump(struct list *l) {

      my_struct *pos, *temp;

      list_for_each(pos, temp, l, node) {
         printf("%s\n", pos->data);
      }
   }
}

using tvec = vector<const char *>;

void check_list_elems(struct list& list_var, const tvec& exp)
{
   size_t i = 0;
   my_struct *pos, *temp;

   list_for_each(pos, temp, &list_var, node) {

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
   struct list list_var = make_list(list_var);

   ASSERT_TRUE(list_is_empty(&list_var));

   ASSERT_TRUE(list_var.first == (struct list_node*)&list_var);
   ASSERT_TRUE(list_var.last == (struct list_node*)&list_var);

   bzero(&list_var, sizeof(list_var));

   list_init(&list_var);
   ASSERT_TRUE(list_var.first == (struct list_node*)&list_var);
   ASSERT_TRUE(list_var.last == (struct list_node*)&list_var);

   check_list_elems(list_var, tvec{});
}


TEST(list_adt, add)
{
   my_struct e1("head");
   my_struct e2("tail");

   ASSERT_TRUE(list_to_obj(&e1.node, my_struct, node) == &e1);
   ASSERT_TRUE(list_to_obj(&e2.node, my_struct, node) == &e2);

   struct list list_var;
   list_init(&list_var);
   ASSERT_TRUE(list_is_empty(&list_var));

   list_add_after((struct list_node *)&list_var, &e1.node);
   list_add_after(&e1.node, &e2.node);

   ASSERT_TRUE(e1.node.next == &e2.node);
   ASSERT_TRUE(e1.node.next->next == (struct list_node *)&list_var);

   ASSERT_TRUE(e1.node.prev == (struct list_node *)&list_var);
   ASSERT_TRUE(e2.node.prev == &e1.node);

   check_list_elems(list_var, tvec{"head", "tail"});

   my_struct e12("mid");
   list_add_after(&e1.node, &e12.node);

   ASSERT_TRUE(e1.node.next == &e12.node);
   ASSERT_TRUE(e1.node.next->next == &e2.node);

   ASSERT_TRUE(e1.node.prev == (struct list_node *)&list_var);
   ASSERT_TRUE(e2.node.prev == &e12.node);

   ASSERT_TRUE(e2.node.next == (struct list_node *)&list_var);
   ASSERT_TRUE(list_var.last == &e2.node);

   ASSERT_TRUE(list_to_obj(&e1.node, my_struct, node) == &e1);
   ASSERT_TRUE(list_to_obj(&e2.node, my_struct, node) == &e2);
   ASSERT_TRUE(list_to_obj(&e12.node, my_struct, node) == &e12);

   check_list_elems(list_var, tvec{"head", "mid", "tail"});
}

TEST(list_adt, add_tail)
{
   my_struct e1("head");
   my_struct e2("tail");

   ASSERT_TRUE(list_to_obj(&e1.node, my_struct, node) == &e1);
   ASSERT_TRUE(list_to_obj(&e2.node, my_struct, node) == &e2);

   struct list list_var;
   list_init(&list_var);

   ASSERT_TRUE(list_is_empty(&list_var));
   list_add_tail(&list_var, &e1.node);
   list_add_tail(&list_var, &e2.node);

   check_list_elems(list_var, tvec{"head", "tail"});

   my_struct ne("new tail");
   list_add_tail(&list_var, &ne.node);

   EXPECT_TRUE(list_var.last == &ne.node);
   EXPECT_TRUE(ne.node.next == (struct list_node *)&list_var);
   EXPECT_TRUE(e2.node.next == &ne.node);
   EXPECT_TRUE(ne.node.prev == &e2.node);

   check_list_elems(list_var, tvec{"head", "tail", "new tail"});
}

TEST(list_adt, remove_elem)
{
   struct list list_var;
   list_init(&list_var);

   my_struct e1("e1"), e2("e2"), e3("e3");

   list_add_tail(&list_var, &e1.node);
   list_add_tail(&list_var, &e2.node);
   list_add_tail(&list_var, &e3.node);
   check_list_elems(list_var, tvec{"e1", "e2", "e3"});

   ASSERT_TRUE(list_is_node_in_list(&e2.node));

   list_remove(&e2.node);
   check_list_elems(list_var, tvec{"e1", "e3"});
   ASSERT_FALSE(list_is_node_in_list(&e2.node));

   // now, remove e2 again and check that nothing really happened
   list_remove(&e2.node);
   check_list_elems(list_var, tvec{"e1", "e3"});
   ASSERT_FALSE(list_is_node_in_list(&e2.node));

   struct list_node a_node;
   list_node_init(&a_node);
   ASSERT_FALSE(list_is_node_in_list(&a_node));
}

TEST(list_adt, add_head)
{
   struct list list_var;
   list_init(&list_var);

   my_struct e1("e1"), e2("e2"), e3("e3");

   list_add_head(&list_var, &e1.node);
   list_add_head(&list_var, &e2.node);
   list_add_head(&list_var, &e3.node);

   check_list_elems(list_var, tvec{"e3", "e2", "e1"});
}
