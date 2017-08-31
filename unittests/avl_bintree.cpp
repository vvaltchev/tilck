

#include <iostream>
#include <random>
#include <unordered_set>

#include <gtest/gtest.h>

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

int check_height(int_struct *obj, bool *failed)
{
   if (!obj)
      return -1;

   assert(obj->node.left_obj != obj);
   assert(obj->node.right_obj != obj);

   bool fail1, fail2;

   int lh = check_height((int_struct*)obj->node.left_obj, &fail1);
   int rh = check_height((int_struct*)obj->node.right_obj, &fail2);

   if (fail1 || fail2) {

      if (failed) {
         *failed = true;
         return -1;
      }

      printf("Tree:\n");
      node_dump(obj, 0);
      NOT_REACHED();
   }

   if ( obj->node.height != ( max(lh, rh) + 1 ) ) {

      printf("[ERROR] obj->node.height != ( max(lh, rh) + 1 ); Node val: %i. H: %i vs %i\n",
             obj->val, obj->node.height, max(lh, rh) + 1);

      if (failed != NULL) {
         *failed = true;
         return -1;
      }

      NOT_REACHED();
   }

   // balance condition.

   if (!( -1 <= (lh-rh) && (lh-rh) <= 1 )) {
      printf("[ERROR] lh-rh is %i\n", lh-rh);

      if (failed != NULL) {
         *failed = true;
         return -1;
      }

      NOT_REACHED();
   }

   (void)lh;
   (void)rh;

   if (failed != NULL)
      *failed = false;

   return obj->node.height;
}

static int cmpfun_objval(const void *obj, const void *valptr)
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

static void in_order_visit_rand(int iters, int elems, bool slow_checks)
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

TEST(avl_bintree, in_order_visit_quick)
{
   in_order_visit_rand(100, 1000, true);
}

TEST(avl_bintree, remove_rand)
{
   const int elems = 32;
   const int iters = 20000;

   random_device rdev;
   const auto seed = 3826494094; //rdev();
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


TEST(avl_bintree, DISABLED_in_order_visit_random_tree_10k_iters_100_elems)
{
   in_order_visit_rand(10*1000, 100, true);
}

TEST(avl_bintree, DISABLED_in_order_visit_random_tree_10_iters_100k_elems)
{
   in_order_visit_rand(10, 100*1000, false);
}

TEST(avl_bintree, DISABLED_in_order_visit_random_tree_1m_elems)
{
   in_order_visit_rand(1, 1000*1000, false);
}

