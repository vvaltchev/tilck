
#include <iostream>
#include <limits>
#include <random>

#include <gtest/gtest.h>

using namespace std;

extern "C" {
   #include <common/string_util.h>
}

#define CHECK_hex(num, bits)                  \
   do {                                       \
      memset(expected, 0, sizeof(expected));  \
      memset(got, 0, sizeof(got));            \
      u64 __val = num;                        \
      sprintf(expected, "%lx", __val);        \
      uitoa##bits##_hex(__val, got);          \
      ASSERT_STREQ(got, expected);            \
   } while (0)

#define CHECK_hex_fixed(num, bits)            \
   do {                                       \
      memset(expected, 0, sizeof(expected));  \
      memset(got, 0, sizeof(got));            \
      u64 __val = num;                        \
      if (bits == 32)                         \
         sprintf(expected, "%08lx", __val);   \
      else                                    \
         sprintf(expected, "%016lx", __val);  \
      uitoa##bits##_hex_fixed(__val, got);    \
      ASSERT_STREQ(got, expected);            \
   } while (0)

#define CHECK_udec(num, bits)                 \
   do {                                       \
      memset(expected, 0, sizeof(expected));  \
      memset(got, 0, sizeof(got));            \
      u64 __val = num;                        \
      sprintf(expected, "%lu", __val);        \
      uitoa##bits##_dec(__val, got);          \
      ASSERT_STREQ(got, expected);            \
   } while (0)



TEST(itoa, u32_hex)
{
   char expected[32];
   char got[32];

   CHECK_hex(0, 32);
   CHECK_hex(numeric_limits<u32>::min(), 32);
   CHECK_hex(numeric_limits<u32>::max(), 32);
   CHECK_hex(numeric_limits<u32>::min() + 1, 32);
   CHECK_hex(numeric_limits<u32>::max() - 1, 32);

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);
   cout << "[ INFO     ] random seed: " << seed << endl;

   lognormal_distribution<> dist(5.0, 3);

   for (int i = 0; i < 100000; i++) {
      CHECK_hex((u32)dist(e), 32);
   }
}

TEST(itoa, u64_hex)
{
   char expected[64];
   char got[64];

   CHECK_hex(0, 64);
   CHECK_hex(numeric_limits<u64>::min(), 64);
   CHECK_hex(numeric_limits<u64>::max(), 64);
   CHECK_hex(numeric_limits<u64>::min() + 1, 64);
   CHECK_hex(numeric_limits<u64>::max() - 1, 64);

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);
   cout << "[ INFO     ] random seed: " << seed << endl;

   lognormal_distribution<> dist(12.0, 4);

   for (int i = 0; i < 100000; i++) {
      CHECK_hex((u64)dist(e), 64);
   }
}

TEST(itoa, u32_dec)
{
   char expected[32];
   char got[32];

   CHECK_udec(0, 32);
   CHECK_udec(numeric_limits<u32>::min(), 32);
   CHECK_udec(numeric_limits<u32>::max(), 32);
   CHECK_udec(numeric_limits<u32>::min() + 1, 32);
   CHECK_udec(numeric_limits<u32>::max() - 1, 32);

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);
   cout << "[ INFO     ] random seed: " << seed << endl;

   lognormal_distribution<> dist(5.0, 3);

   for (int i = 0; i < 100000; i++) {
      CHECK_udec((u32)dist(e), 32);
   }
}

TEST(itoa, u64_dec)
{
   char expected[64];
   char got[64];

   CHECK_udec(0, 64);
   CHECK_udec(numeric_limits<u64>::min(), 64);
   CHECK_udec(numeric_limits<u64>::max(), 64);
   CHECK_udec(numeric_limits<u64>::min() + 1, 64);
   CHECK_udec(numeric_limits<u64>::max() - 1, 64);

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);
   cout << "[ INFO     ] random seed: " << seed << endl;

   lognormal_distribution<> dist(12.0, 4.0);

   for (int i = 0; i < 100000; i++) {
      CHECK_udec((u32)dist(e), 64);
   }
}

TEST(itoa, u32_hex_fixed)
{
   char expected[32];
   char got[32];

   CHECK_hex_fixed(0, 32);
   CHECK_hex_fixed(numeric_limits<u32>::min(), 32);
   CHECK_hex_fixed(numeric_limits<u32>::max(), 32);
   CHECK_hex_fixed(numeric_limits<u32>::min() + 1, 32);
   CHECK_hex_fixed(numeric_limits<u32>::max() - 1, 32);

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);
   cout << "[ INFO     ] random seed: " << seed << endl;

   lognormal_distribution<> dist(5.0, 3);

   for (int i = 0; i < 100000; i++) {
      CHECK_hex_fixed((u32)dist(e), 32);
   }
}
