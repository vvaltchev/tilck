
#include <cstdio>
#include <gtest/gtest.h>

using namespace std;
using namespace testing;

extern "C" {
   #include <exos/sort.h>
}

static int less_than_cmp_int(const void *a, const void *b)
{
   const int *a_val = (const int *)a;
   const int *b_val = (const int *)b;

   if (a < b)
      return -1;

   if (a == b)
      return 0;

   return 1;
}

static bool my_is_sorted(uptr *arr, int len, cmpfun_ptr cmp)
{
   if (len <= 1)
      return true;

   for (int i = 0; i < len - 1; i++)
      if (cmp(&arr[i], &arr[i+1]) > 0)
         return false;

   return true;
}

TEST(insertion_sort_ptr, basic_test)
{
   int vec[] = { 3, 4, 1, 0, -3, 10, 2 };

   insertion_sort_ptr((uptr *)&vec, ARRAY_SIZE(vec), less_than_cmp_int);
   ASSERT_TRUE(my_is_sorted((uptr *)vec, ARRAY_SIZE(vec), less_than_cmp_int));
}


TEST(insertion_sort_generic, basic_test)
{
   int vec[] = { 3, 4, 1, 0, -3, 10, 2 };

   insertion_sort_generic((uptr *)&vec, sizeof(int),
                          ARRAY_SIZE(vec), less_than_cmp_int);
   ASSERT_TRUE(my_is_sorted((uptr *)vec, ARRAY_SIZE(vec), less_than_cmp_int));
}
