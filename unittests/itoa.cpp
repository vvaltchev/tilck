
#include <iostream>
#include <limits>
#include <random>

#include <gtest/gtest.h>

using namespace std;

extern "C" {
   #include <common/string_util.h>
}

#define CHECK_u32_hex(num)               \
   do {                                  \
      u32 __val = num;                   \
      sprintf(expected, "%x", __val);    \
      uitoa32(__val, got, 16);           \
      ASSERT_STREQ(got, expected);       \
   } while (0)


TEST(itoa, u32_hex)
{
   char expected[32];
   char got[32];

   CHECK_u32_hex(0);
   CHECK_u32_hex(numeric_limits<u32>::min());
   CHECK_u32_hex(numeric_limits<u32>::max());
   CHECK_u32_hex(numeric_limits<u32>::min() + 1);
   CHECK_u32_hex(numeric_limits<u32>::max() - 1);

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);
   cout << "[ INFO     ] random seed: " << seed << endl;

   lognormal_distribution<> dist(5.0, 3);

   for (int i = 0; i < 100000; i++) {
      CHECK_u32_hex((u32)dist(e));
   }
}
