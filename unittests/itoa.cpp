
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <gtest/gtest.h>

using namespace std;

extern "C" {
   #include <exos/common/string_util.h>
}


// Wrapper defined only for s32, s64, u32, u64.
template <typename T>
inline void itoa_wrapper(T val, char *buf);

// Wrapper defined only for u32, u64.
template <typename T>
inline void uitoa_hex_wrapper(T val, char *buf, bool fixed);

// Wrapper defined only for u32, u64.
template <typename T>
inline void sprintf_hex_wrapper(T val, char *buf, bool fixed);

template <>
inline void itoa_wrapper<s32>(s32 val, char *buf) { itoa32(val, buf); }

template <>
inline void itoa_wrapper<s64>(s64 val, char *buf) { itoa64(val, buf); }

template <>
inline void itoa_wrapper<u32>(u32 val, char *buf) { uitoa32_dec(val, buf); }

template <>
inline void itoa_wrapper<u64>(u64 val, char *buf) { uitoa64_dec(val, buf); }


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
typename enable_if<hex, void>::type
check(T val)
{
   char expected[64];
   char got[64];

   memset(expected, '*', sizeof(expected));
   memset(got, '*', sizeof(got));

   sprintf_hex_wrapper<T>(val, expected, fixed);
   uitoa_hex_wrapper<T>(val, got, fixed);

   ASSERT_STREQ(got, expected);
}

template <typename T, bool hex = false, bool fixed = false>
typename enable_if<!hex, void>::type
check(T val)
{
   char expected[64];
   char got[64];

   memset(expected, '*', sizeof(expected));
   memset(got, '*', sizeof(got));

   strcpy(expected, to_string(val).c_str());
   itoa_wrapper(val, got);

   ASSERT_STREQ(got, expected);
}


template <typename T, bool hex = false, bool fixed = false>
void check_basic_set()
{
   auto check_func = check<T, hex, fixed>;

   ASSERT_NO_FATAL_FAILURE({
      check_func(numeric_limits<T>::lowest());
      check_func(numeric_limits<T>::min());
      check_func(numeric_limits<T>::max());
      check_func(numeric_limits<T>::min() + 1);
      check_func(numeric_limits<T>::max() - 1);
   });
}

template <typename T, bool hex = false, bool fixed = false>
void generic_itoa_test_body()
{
   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);

   auto check_func = check<T, hex, fixed>;
   auto check_basic_set_func = check_basic_set<T, hex, fixed>;

   uniform_int_distribution<T> dist(numeric_limits<T>::min(),
                                    numeric_limits<T>::max());

   ASSERT_NO_FATAL_FAILURE({ check_basic_set_func(); });

   for (int i = 0; i < 100000; i++)
      ASSERT_NO_FATAL_FAILURE({ check_func(dist(e)); });
}

TEST(itoa, u32_dec)
{
   auto test_func = generic_itoa_test_body<u32>;
   ASSERT_NO_FATAL_FAILURE({ test_func(); });
}

TEST(itoa, u64_dec)
{
   auto test_func = generic_itoa_test_body<u64>;
   ASSERT_NO_FATAL_FAILURE({ test_func(); });
}

TEST(itoa, s32_dec)
{
   auto test_func = generic_itoa_test_body<s32>;
   ASSERT_NO_FATAL_FAILURE({ test_func(); });
}

TEST(itoa, s64_dec)
{
   auto test_func = generic_itoa_test_body<s64>;
   ASSERT_NO_FATAL_FAILURE({ test_func(); });
}

TEST(itoa, u32_hex)
{
   auto test_func = generic_itoa_test_body<u32, true>;
   ASSERT_NO_FATAL_FAILURE({ test_func(); });
}

TEST(itoa, u64_hex)
{
   auto test_func = generic_itoa_test_body<u64, true>;
   ASSERT_NO_FATAL_FAILURE({ test_func(); });
}

TEST(itoa, u32_hex_fixed)
{
   auto test_func = generic_itoa_test_body<u32, true, true>;
   ASSERT_NO_FATAL_FAILURE({ test_func(); });
}

TEST(itoa, u64_hex_fixed)
{
   auto test_func = generic_itoa_test_body<u64, true, true>;
   ASSERT_NO_FATAL_FAILURE({ test_func(); });
}


TEST(exos_strtol, basic_tests)
{
   EXPECT_EQ(exos_strtol("0", NULL, NULL), 0);
   EXPECT_EQ(exos_strtol("1", NULL, NULL), 1);
   EXPECT_EQ(exos_strtol("12", NULL, NULL), 12);
   EXPECT_EQ(exos_strtol("123", NULL, NULL), 123);
   EXPECT_EQ(exos_strtol("-1", NULL, NULL), -1);
   EXPECT_EQ(exos_strtol("-123", NULL, NULL), -123);
   EXPECT_EQ(exos_strtol("2147483647", NULL, NULL), 2147483647); // INT_MAX
   EXPECT_EQ(exos_strtol("2147483648", NULL, NULL), 0); // INT_MAX + 1
   EXPECT_EQ(exos_strtol("-2147483648", NULL, NULL), -2147483648); // INT_MIN
   EXPECT_EQ(exos_strtol("-2147483649", NULL, NULL), 0); // INT_MIN - 1
   EXPECT_EQ(exos_strtol("123abc", NULL, NULL), 123);
   EXPECT_EQ(exos_strtol("123 abc", NULL, NULL), 123);
   EXPECT_EQ(exos_strtol("-123abc", NULL, NULL), -123);
}

TEST(exos_strtol, errors)
{
   const char *str;
   const char *endptr;
   int error;
   int res;

   str = "abc";
   res = exos_strtol(str, &endptr, &error);
   EXPECT_EQ(res, 0);
   EXPECT_EQ(endptr, str);
   EXPECT_EQ(error, -EINVAL);

   str = "2147483648"; // INT_MAX + 1
   res = exos_strtol(str, &endptr, &error);
   EXPECT_EQ(res, 0);
   EXPECT_EQ(endptr, str);
   EXPECT_EQ(error, -ERANGE);
}
