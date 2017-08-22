

#include <iostream>
#include <random>
#include <gtest/gtest.h>

#include <common_defs.h>

extern "C" {
   #include <bintree.h>
}

using namespace std;

struct int_struct {

   int val;
   bintree_node node;

   int_struct() = default;

   int_struct(int v) {
      val = v;
      bintree_node_init(&node);
   }
};


static int my_cmpfun(const void *a, const void *b)
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


   if (!n->left_obj && !n->right_obj)
      return;

   if (n->left_obj) {
      node_dump((int_struct*)n->left_obj, level+1);
   } else {
      indent(level+1);
      printf("L%i\n", obj->val);
   }

   if (n->right_obj) {
      node_dump((int_struct*)n->right_obj, level+1);
   } else {
      indent(level+1);
      printf("R%i\n", obj->val);
   }
}

// TEST(avl_bintree, basic_insert_test)
// {
//    int_struct *r = new int_struct(100);
//    int_struct *n2 = new int_struct(50);
//    int_struct *n3 = new int_struct(200);
//    int_struct *n4 = new int_struct(25);
//    int_struct *n5 = new int_struct(75);

//    bintree_insert(&r, n2, my_cmpfun, int_struct, node);
//    bintree_insert(&r, n3, my_cmpfun, int_struct, node);
//    bintree_insert(&r, n4, my_cmpfun, int_struct, node);
//    bintree_insert(&r, n5, my_cmpfun, int_struct, node);

//    node_dump(r, 0);
// }

// TEST(avl_bintree, insert_ordered_numbers)
// {
//    constexpr int elems = 32;
//    int_struct *arr[elems];

//    for (int i = 0; i < elems; i++)
//       arr[i] = new int_struct(i + 1);

//    for (int i = 1; i < elems; i++)
//       bintree_insert(&arr[0], arr[i], my_cmpfun, int_struct, node);

//    node_dump(arr[0], 0);
// }

static void
in_order_visit_int(int_struct *obj,
                   int *arr,
                   int arr_size,
                   int *curr_size)
{
   if (!obj) return;

   bintree_node *n = &obj->node;

   in_order_visit_int((int_struct*)n->left_obj, arr, arr_size, curr_size);

   assert(*curr_size < arr_size);
   arr[(*curr_size)++] = obj->val;

   in_order_visit_int((int_struct*)n->right_obj, arr, arr_size, curr_size);
}

static void
in_order_visit(int_struct *obj,
               int *arr,
               int arr_size)
{
   int curr_size = 0;
   in_order_visit_int(obj, arr, arr_size, &curr_size);
}

static bool
check_binary_search_tree(int_struct *obj)
{
   if (obj->node.left_obj) {

      int leftval = ((int_struct*)(obj->node.left_obj))->val;

      if (leftval >= obj->val) {
         printf("left child of %i has value %i, which violates BST\n",
                obj->val, leftval);
         return false;
      }

      if (!check_binary_search_tree((int_struct*)obj->node.left_obj))
         return false;
   }

   if (obj->node.right_obj) {
      int rightval = ((int_struct*)(obj->node.right_obj))->val;

      if (rightval <= obj->val) {
         printf("right child of %i has value %i, which violates BST\n",
                obj->val, rightval);
         return false;
      }

      if (!check_binary_search_tree((int_struct*)obj->node.right_obj))
         return false;
   }

   return true;
}

static bool is_sorted(int *arr, int size)
{
   if (size <= 1)
      return true;

   for (int i = 1; i < size; i++) {
      if (arr[i-1] > arr[i])
         return false;
   }

   return true;
}

TEST(avl_bintree, in_order_visit_correct)
{
   constexpr const int elems = 32;
   int_struct arr[elems];

   for (int i = 0; i < elems; i++)
      arr[i] = int_struct(i + 1);

   int_struct *root = &arr[0];

   for (int i = 1; i < elems; i++)
      bintree_insert(&root, &arr[i], my_cmpfun, int_struct, node);

   int ordered_nums[elems];
   in_order_visit(root, ordered_nums, elems);
   ASSERT_TRUE(is_sorted(ordered_nums, elems));
}

static void dump_array(int *arr, int size)
{
   for (int i = 0; i < size; i++) {
      printf("%i\n", arr[i]);
   }

   printf("\n");
}

static bool exists_in_array(int e, int *arr, int arr_size)
{
   for (int i = 0; i < arr_size; i++)
      if (arr[i] == e)
         return true;

   return false;
}

static void
generate_random_array(default_random_engine &e,
                      lognormal_distribution<> &dist,
                      int *arr,
                      int arr_size)
{
   for (int i = 0; i < arr_size; i++) {

      int candidate;

      do {
         candidate = dist(e);
      } while (exists_in_array(candidate, arr, i));

      arr[i] = candidate;
   }
}

TEST(avl_bintree, in_order_visit_random_tree)
{
   constexpr const int iters = 100;
   constexpr const int elems = 15;

   random_device rdev;
   default_random_engine e(rdev());
   lognormal_distribution<> dist(5.0, 3);

   int arr[elems];
   int ordered_nums[elems];
   int_struct nodes[elems];
   int_struct *root = &nodes[0];

   for (int iter = 0; iter < iters; iter++) {

      generate_random_array(e, dist, arr, elems);

      for (int i = 0; i < elems; i++) {
         nodes[i] = int_struct(arr[i]);
         bintree_insert(&root, &nodes[i], my_cmpfun, int_struct, node);

         if (!check_binary_search_tree(root)) {
            node_dump(root, 0);
            FAIL() << "while inserting node " << arr[i] << endl;
         }
      }

      in_order_visit(root, ordered_nums, elems);

      if (!is_sorted(ordered_nums, elems)) {
         printf("FAIL. Original:\n");
         dump_array(arr, elems);
         printf("Ordered:\n");
         dump_array(ordered_nums, elems);
         printf("Tree:\n");
         node_dump(root, 0);
         FAIL();
      }
   }
}
