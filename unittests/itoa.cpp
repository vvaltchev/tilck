
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <gtest/gtest.h>

using namespace std;

extern "C" {
   #include <common/string_util.h>
}

inline void itoa_wrapper(s32 val, char *buf) { itoa32(val, buf); }
inline void itoa_wrapper(s64 val, char *buf) { itoa64(val, buf); }
inline void itoa_wrapper(u32 val, char *buf) { uitoa32_dec(val, buf); }
inline void itoa_wrapper(u64 val, char *buf) { uitoa64_dec(val, buf); }

template <typename T>
inline void uitoa_hex_wrapper(T val, char *buf, bool fixed)
{
   abort();
}

template<>
inline void uitoa_hex_wrapper<u32>(u32 val, char *buf, bool fixed)
{
   if (!fixed)
      uitoa32_hex(val, buf);
   else
      uitoa32_hex_fixed(val, buf);
}

template<>
inline void uitoa_hex_wrapper<u64>(u64 val, char *buf, bool fixed)
{
   if (!fixed)
      uitoa64_hex(val, buf);
   else
      uitoa64_hex_fixed(val, buf);
}


template <typename T>
inline void sprintf_hex_wrapper(T val, char *buf, bool fixed)
{
   abort();
}

template <>
inline void sprintf_hex_wrapper<u32>(u32 val, char *buf, bool fixed)
{
   if (fixed)
      sprintf(buf, "%08x", val);
   else
      sprintf(buf, "%x", val);
}

template <>
inline void sprintf_hex_wrapper<u64>(u64 val, char *buf, bool fixed)
{
   if (fixed)
      sprintf(buf, "%016lx", val);
   else
      sprintf(buf, "%lx", val);
}


template <typename T, bool hex = false, bool fixed = false>
void check(T val)
{
   char expected[64];
   char got[64];

   memset(expected, '*', sizeof(expected));
   memset(got, '*', sizeof(got));

   if (!hex) {

      strcpy(expected, to_string(val).c_str());
      itoa_wrapper(val, got);

   } else {

      sprintf_hex_wrapper<T>(val, expected, fixed);
      uitoa_hex_wrapper<T>(val, got, fixed);

   }

   ASSERT_STREQ(got, expected);
}

template <typename T, bool hex = false, bool fixed = false>
void check_set()
{
   auto check_func = check<T, hex, fixed>;

   check_func(0);
   check_func(numeric_limits<u32>::min());
   check_func(numeric_limits<u32>::max());
   check_func(numeric_limits<u32>::min() + 1);
   check_func(numeric_limits<u32>::max() - 1);
}

TEST(itoa, u32_hex)
{
   typedef u32 T;
   auto check_func = check<T, true>;
   auto check_set_func = check_set<T, true>;

   ASSERT_NO_FATAL_FAILURE({ check_set_func(); });

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);

   uniform_int_distribution<T>
      dist(numeric_limits<T>::min(), numeric_limits<T>::max());

   for (int i = 0; i < 100000; i++)
      ASSERT_NO_FATAL_FAILURE({ check_func(dist(e)); });
}

TEST(itoa, u64_hex)
{
   typedef u64 T;
   auto check_func = check<T, true>;
   auto check_set_func = check_set<T, true>;

   ASSERT_NO_FATAL_FAILURE({ check_set_func(); });

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);

   uniform_int_distribution<T>
      dist(numeric_limits<T>::min(), numeric_limits<T>::max());

   for (int i = 0; i < 100000; i++)
      ASSERT_NO_FATAL_FAILURE({ check_func(dist(e)); });
}

TEST(itoa, u32_dec)
{
   typedef u32 T;
   auto check_func = check<T>;
   auto check_set_func = check_set<T>;

   ASSERT_NO_FATAL_FAILURE({ check_set_func(); });

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);

   uniform_int_distribution<T>
      dist(numeric_limits<T>::min(), numeric_limits<T>::max());

   for (int i = 0; i < 100000; i++)
      ASSERT_NO_FATAL_FAILURE({ check_func(dist(e)); });
}

TEST(itoa, u64_dec)
{
   typedef u64 T;
   auto check_func = check<T>;
   auto check_set_func = check_set<T>;

   ASSERT_NO_FATAL_FAILURE({ check_set_func(); });

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);

   uniform_int_distribution<T>
      dist(numeric_limits<T>::min(), numeric_limits<T>::max());

   for (int i = 0; i < 100000; i++)
      ASSERT_NO_FATAL_FAILURE({ check_func(dist(e)); });
}

TEST(itoa, u32_hex_fixed)
{
   typedef u32 T;
   auto check_func = check<T, true, true>;
   auto check_set_func = check_set<T, true, true>;

   ASSERT_NO_FATAL_FAILURE({ check_set_func(); });

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);

   uniform_int_distribution<T>
      dist(numeric_limits<T>::min(), numeric_limits<T>::max());

   for (int i = 0; i < 100000; i++)
      ASSERT_NO_FATAL_FAILURE({ check_func(dist(e)); });
}

TEST(itoa, u64_hex_fixed)
{
   typedef u64 T;
   auto check_func = check<T, true, true>;
   auto check_set_func = check_set<T, true, true>;

   ASSERT_NO_FATAL_FAILURE({ check_set_func(); });

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);

   uniform_int_distribution<T>
      dist(numeric_limits<T>::min(), numeric_limits<T>::max());

   for (int i = 0; i < 100000; i++)
      ASSERT_NO_FATAL_FAILURE({ check_func(dist(e)); });
}

TEST(itoa, s32_dec)
{
   typedef s32 T;
   auto check_func = check<T>;
   auto check_set_func = check_set<T>;

   ASSERT_NO_FATAL_FAILURE({ check_set_func(); });

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);

   uniform_int_distribution<T>
      dist(numeric_limits<T>::min(), numeric_limits<T>::max());

   for (int i = 0; i < 100000; i++)
      ASSERT_NO_FATAL_FAILURE({ check_func(dist(e)); });
}

TEST(itoa, s64_dec)
{
   typedef s32 T;
   auto check_func = check<T>;
   auto check_set_func = check_set<T>;

   ASSERT_NO_FATAL_FAILURE({ check_set_func(); });

   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);

   uniform_int_distribution<T>
      dist(numeric_limits<T>::min(), numeric_limits<T>::max());

   for (int i = 0; i < 100000; i++)
      ASSERT_NO_FATAL_FAILURE({ check_func(dist(e)); });
}

