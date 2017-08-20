

#include <iostream>
#include <gtest/gtest.h>

#include <common_defs.h>

extern "C" {
#include <bintree.h>

void rotate_cw_left_child(bintree_node *n);
}

using namespace std;

struct int_struct {

   int val;
   bintree_node node;

   int_struct(int v) {
      val = v;
      bintree_node_init(&node);
   }
};


int my_cmpfun(const void *a, const void *b)
{
   int_struct *v1 = (int_struct*)a;
   int_struct *v2 = (int_struct*)b;

   return v1->val - v2->val;
}

////////////////////////////////////////////////////////////////////////////
// DEBUGGING STUFF
////////////////////////////////////////////////////////////////////////////


static void indent(int level)
{
   for (int i=0; i < level; i++)
      printf("  ");
}

static void node_dump(int_struct *obj, int level)
{
   if (!obj) return;

   indent(level);
   bintree_node *n = &obj->node;

   printf("%i [%i]\n", obj->val, n->height);


   if (!n->left && !n->right)
      return;

   if (n->left) {
      node_dump((int_struct*)n->left, level+1);
   } else {
      indent(level+1);
      printf("#L%i\n", obj->val);
   }

   if (n->right) {
      node_dump((int_struct*)n->right, level+1);
   } else {
      indent(level+1);
      printf("#R%i\n", obj->val);
   }
}

TEST(avl_bintree, insert_test)
{
   int_struct *r = new int_struct(100);
   int_struct *n2 = new int_struct(50);
   int_struct *n3 = new int_struct(200);
   int_struct *n4 = new int_struct(25);
   int_struct *n5 = new int_struct(75);

   bintree_insert(&r, n2, my_cmpfun, int_struct, node);
   bintree_insert(&r, n3, my_cmpfun, int_struct, node);
   bintree_insert(&r, n4, my_cmpfun, int_struct, node);
   bintree_insert(&r, n5, my_cmpfun, int_struct, node);

   node_dump(r, 0);
}

// TEST(avl_bintree, rotate_cw_left_child)
// {
//    int_struct r(3);
//    int_struct n2(2);
//    int_struct n3(1);
//    bintree_insert(&r.node, &n2.node, my_cmpfun, int_struct, node);
//    bintree_insert(&r.node, &n3.node, my_cmpfun, int_struct, node);
//    node_dump(&r.node, 0);

//    rotate_cw_left_child(&r.node);
//    node_dump(&r.node, 0);
// }
