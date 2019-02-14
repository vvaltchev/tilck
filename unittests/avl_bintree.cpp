/* SPDX-License-Identifier: BSD-2-Clause */

#include <iostream>
#include <random>
#include <unordered_set>
#include <memory>

#include <gtest/gtest.h>

extern "C" {
   #include <tilck/kernel/bintree.h>
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


static sptr my_cmpfun(const void *a, const void *b)
{
   int_struct *v1 = (int_struct*)a;
   int_struct *v2 = (int_struct*)b;

   return v1->val - v2->val;
}

/*
 * --------------------------------------------------------------------------
 * DEBUGGING UTILS
 * --------------------------------------------------------------------------
 */


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

struct visit_ctx {
   int *arr;
   int arr_size;
   int curr_size;
};

static int visit_add_val_to_arr(void *obj, void *arg)
{
   int_struct *s = (int_struct *)obj;
   visit_ctx *ctx = (visit_ctx *)arg;

   assert(ctx->curr_size < ctx->arr_size);
   ctx->arr[ctx->curr_size++] = s->val;

   return 0;
}

static void
in_order_visit(int_struct *obj,
               int *arr,
               int arr_size)
{
   visit_ctx ctx = {arr, arr_size, 0};

   bintree_in_order_visit(obj,
                          visit_add_val_to_arr,
                          (void *)&ctx,
                          int_struct,
                          node);
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


static void dump_array(int *arr, int size)
{
   for (int i = 0; i < size; i++) {
      printf("%i\n", arr[i]);
   }

   printf("\n");
}

static void
generate_random_array(default_random_engine &e,
                      lognormal_distribution<> &dist,
                      int *arr,
                      int arr_size)
{
   unordered_set<int> s;

   for (int i = 0; i < arr_size;) {

      int candidate = dist(e);
      if (candidate <= 0 || candidate > 1000*1000*1000) continue;

      if (s.insert(candidate).second) {
         arr[i++] = candidate;
      }
   }

   assert(s.size() == (size_t)arr_size);

   for (int i = 0; i < arr_size; i++) {
      assert(arr[i] > 0);
   }
}

// TODO: re-write this function in a better way.
int check_height(int_struct *obj, bool *failed)
{
   if (!obj) {
      if (failed)
         *failed=false;
      return -1;
   }

   assert(obj->node.left_obj != obj);
   assert(obj->node.right_obj != obj);

   bool fail1 = false, fail2 = false;

   int lh = check_height((int_struct*)obj->node.left_obj, &fail1);
   int rh = check_height((int_struct*)obj->node.right_obj, &fail2);

   if (fail1 || fail2) {

      if (failed) {
         *failed = true;
         return -1000;
      }

      printf("Tree:\n");
      node_dump(obj, 0);
      NOT_REACHED();
   }

   if ( obj->node.height != ( max(lh, rh) + 1 ) ) {

      printf("[ERROR] obj->node.height != ( max(lh, rh) + 1 ); "
             "Node val: %i. H: %i vs %i\n", obj->val,
             obj->node.height, max(lh, rh) + 1);

      printf("Tree:\n");
      node_dump(obj, 0);

      if (failed != NULL) {
         *failed = true;
         return -1000;
      }

      NOT_REACHED();
   }

   // balance condition.

   if (!( -1 <= (lh-rh) && (lh-rh) <= 1 )) {
      printf("[ERROR] lh-rh is %i for node %i; lh:%i, rh:%i\n",
             lh-rh, obj->val, lh, rh);

      printf("Tree:\n");
      node_dump(obj, 0);

      if (failed != NULL) {
         *failed = true;
         return -1000;
      }

      NOT_REACHED();
   }

   if (failed != NULL)
      *failed = false;

   return max(lh, rh) + 1;
}

static sptr cmpfun_objval(const void *obj, const void *valptr)
{
   int_struct *s = (int_struct*)obj;
   int ival = *(int*)valptr;
   return s->val - ival;
}

#define MAX_ELEMS (1000*1000)

struct test_data {
   int arr[MAX_ELEMS];
   int ordered_nums[MAX_ELEMS];
   int_struct nodes[MAX_ELEMS];
};

void check_height_vs_elems(int_struct *obj, int elems)
{

   /*
    * According to wikipedia:
    * https://en.wikipedia.org/wiki/AVL_tree
    *
    * max_h is the upper-limit for the function height(N) for an AVL tree.
    */
   const int max_h = ceil(1.44 * log2(elems+2) - 0.328);

   if (obj->node.height >= max_h) {

      FAIL() << "tree's height ("
             << obj->node.height
             << ") exceeds the maximum expected: " << max_h-1;
   }
}


/*
 * --------------------------------------------------------------------------
 * TESTS
 * --------------------------------------------------------------------------
 */


TEST(avl_bintree, in_order_visit_after_insert_is_correct)
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

static void test_insert_rand_data(int iters, int elems, bool slow_checks)
{
   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);
   lognormal_distribution<> dist(6.0, elems <= 100*1000 ? 3 : 5);

   unique_ptr<test_data> data{new test_data};

   for (int iter = 0; iter < iters; iter++) {

      int_struct *root = &data->nodes[0];
      generate_random_array(e, dist, data->arr, elems);

      if (iter == 0) {
         cout << "[ INFO     ] random seed: " << seed << endl;
         cout << "[ INFO     ] sample numbers: ";
         for (int i = 0; i < 20 && i < elems; i++) {
            printf("%i ", data->arr[i]);
         }
         printf("\n");
      }

      for (int i = 0; i < elems; i++) {

         data->nodes[i] = int_struct(data->arr[i]);
         bintree_insert(&root, &data->nodes[i], my_cmpfun, int_struct, node);

         if (slow_checks && !check_binary_search_tree(root)) {
            node_dump(root, 0);
            FAIL() << "[iteration " << iter
                   << "/" << iters << "] while inserting node "
                   << data->arr[i] << endl;
         }
      }

      ASSERT_NO_FATAL_FAILURE({ check_height_vs_elems(root, elems); });
      check_height(root, NULL);
      in_order_visit(root, data->ordered_nums, elems);

      if (!is_sorted(data->ordered_nums, elems)) {

         // For a few elems, it makes sense to print more info.
         if (elems <= 100) {
            printf("FAIL. Original:\n");
            dump_array(data->arr, elems);
            printf("Ordered:\n");
            dump_array(data->ordered_nums, elems);
            printf("Tree:\n");
            node_dump(root, 0);
         }
         FAIL() << "an in-order visit did not produce an ordered-array";
      }

      int elems_to_find = slow_checks ? elems : elems/10;

      for (int i = 0; i < elems_to_find; i++) {

         void *res = bintree_find(root, &data->arr[i],
                                  cmpfun_objval, int_struct, node);

         ASSERT_TRUE(res != NULL);
         ASSERT_TRUE(((int_struct*)res)->val == data->arr[i]);
      }
   }
}

TEST(avl_bintree, insert_quick_test)
{
   test_insert_rand_data(100, 1000, true);
}

void remove_rand_data(const int elems, const int iters)
{
   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);
   lognormal_distribution<> dist(6.0, elems <= 100*1000 ? 3 : 5);

   unique_ptr<test_data> data{new test_data};

   for (int iter = 0; iter < iters; iter++) {

      int_struct *root = &data->nodes[0];
      generate_random_array(e, dist, data->arr, elems);

      if (iter == 0) {
         cout << "[ INFO     ] random seed: " << seed << endl;
         cout << "[ INFO     ] sample numbers: ";
         for (int i = 0; i < 20 && i < elems; i++) {
            printf("%i ", data->arr[i]);
         }
         printf("\n");
      }

      for (int i = 0; i < elems; i++) {
         data->nodes[i] = int_struct(data->arr[i]);
         bintree_insert(&root, &data->nodes[i], my_cmpfun, int_struct, node);
      }

      for (int i = 0; i < elems; i++) {

         void *res = bintree_find(root, &data->arr[i],
                                  cmpfun_objval, int_struct, node);

         ASSERT_TRUE(res != NULL);
         ASSERT_TRUE(((int_struct*)res)->val == data->arr[i]);

         void *removed_obj =
            bintree_remove(&root, &data->arr[i],
                           cmpfun_objval, int_struct, node);

         ASSERT_TRUE(removed_obj != NULL);
         ASSERT_TRUE(((int_struct*)removed_obj)->val == data->arr[i]);

         const int new_elems = elems - i - 1;

         if (new_elems == 0) {
            ASSERT_TRUE(root == NULL);
            break;
         }

         ASSERT_NO_FATAL_FAILURE({ check_height_vs_elems(root, elems); });
         check_height(root, NULL);
         in_order_visit(root, data->ordered_nums, new_elems);

         if (!is_sorted(data->ordered_nums, new_elems)) {

            // For a few elems, it makes sense to print more info.
            if (elems <= 100) {
               printf("FAIL. Original:\n");
               dump_array(data->arr, new_elems);
               printf("Ordered:\n");
               dump_array(data->ordered_nums, new_elems);
               printf("Tree:\n");
               node_dump(root, 0);
            }
            FAIL() << "an in-order visit did not produce "
                   << "an ordered-array, after removing " << data->arr[i];
         }
      }

   }
}

TEST(avl_bintree, remove_quick_test)
{
   remove_rand_data(100, 1000);
}

TEST(avl_bintree, DISABLED_remove_1000_elems_100_iters)
{
   remove_rand_data(1000, 100);
}

TEST(avl_bintree, DISABLED_remove_1k_elems_1k_iters)
{
   remove_rand_data(1000, 1000);
}

TEST(avl_bintree, DISABLED_remove_10k_elems_10_iters)
{
   remove_rand_data(10*1000, 10);
}

TEST(avl_bintree, DISABLED_test_insert_rand_data_tree_10k_iters_100_elems)
{
   test_insert_rand_data(10*1000, 100, true);
}

TEST(avl_bintree, DISABLED_test_insert_rand_data_tree_10_iters_100k_elems)
{
   test_insert_rand_data(10, 100*1000, false);
}

TEST(avl_bintree, DISABLED_test_insert_rand_data_tree_1m_elems)
{
   test_insert_rand_data(1, 1000*1000, false);
}

