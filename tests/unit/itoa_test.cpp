/* SPDX-License-Identifier: BSD-2-Clause */

#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <inttypes.h>
#include <gtest/gtest.h>

using namespace std;

extern "C" {
   #include <tilck/common/string_util.h>
   #include <tilck/kernel/test/itoa.h>
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
inline void itoa_wrapper<u32>(u32 val, char *buf) { uitoa32(val, buf, 10); }

template <>
inline void itoa_wrapper<u64>(u64 val, char *buf) { uitoa64(val, buf, 10); }

// strtol() wrapper (s32 and s64 only)
template <typename T>
inline T strtol_wrapper(const char *s, int base);

template <>
inline s32 strtol_wrapper<s32>(const char *s, int base) {
   return tilck_strtol32(s, NULL, base, NULL);
}

template <>
inline s64 strtol_wrapper<s64>(const char *s, int base) {
   return tilck_strtol64(s, NULL, base, NULL);
}

template <>
inline u32 strtol_wrapper<u32>(const char *s, int base) {
   return tilck_strtoul32(s, NULL, base, NULL);
}

template <>
inline u64 strtol_wrapper<u64>(const char *s, int base) {
   return tilck_strtoul64(s, NULL, base, NULL);
}


template<>
inline void uitoa_hex_wrapper<u32>(u32 val, char *buf, bool fixed)
{
   if (!fixed)
      uitoa32(val, buf, 16);
   else
      uitoa32_hex_fixed(val, buf);
}

template<>
inline void uitoa_hex_wrapper<u64>(u64 val, char *buf, bool fixed)
{
   if (!fixed)
      uitoa64(val, buf, 16);
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
      sprintf(buf, "%016" PRIx64, val);
   else
      sprintf(buf, "%" PRIx64, val);
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


TEST(tilck_strtol, basic_tests)
{
   EXPECT_EQ(strtol_wrapper<s32>("0", 10), 0);
   EXPECT_EQ(strtol_wrapper<s32>("1", 10), 1);
   EXPECT_EQ(strtol_wrapper<s32>("12", 10), 12);
   EXPECT_EQ(strtol_wrapper<s32>("123", 10), 123);
   EXPECT_EQ(strtol_wrapper<s32>("-1", 10), -1);
   EXPECT_EQ(strtol_wrapper<s32>("-123", 10), -123);
   EXPECT_EQ(strtol_wrapper<s32>("00123", 10), 123);
   EXPECT_EQ(strtol_wrapper<s32>("2147483647", 10), 2147483647); // INT_MAX
   EXPECT_EQ(strtol_wrapper<s32>("2147483648", 10), 0); // INT_MAX + 1
   EXPECT_EQ(strtol_wrapper<s32>("-2147483648", 10), -2147483648); // INT_MIN
   EXPECT_EQ(strtol_wrapper<s32>("-2147483649", 10), 0); // INT_MIN - 1
   EXPECT_EQ(strtol_wrapper<s32>("123abc", 10), 123);
   EXPECT_EQ(strtol_wrapper<s32>("123 abc", 10), 123);
   EXPECT_EQ(strtol_wrapper<s32>("-123abc", 10), -123);

   EXPECT_EQ(strtol_wrapper<s32>("a", 16), 10);
   EXPECT_EQ(strtol_wrapper<s32>("ff", 16), 255);
   EXPECT_EQ(strtol_wrapper<s32>("02bbffdd", 16), 0x02bbffdd);
   EXPECT_EQ(strtol_wrapper<s32>("111001", 2), 0b111001);
   EXPECT_EQ(strtol_wrapper<s32>("755", 8), 0755);
}

TEST(tilck_strtoll, basic_tests)
{
   EXPECT_EQ(strtol_wrapper<s64>("2147483648", 10), 2147483648); // INT_MAX+1
   EXPECT_EQ(strtol_wrapper<s64>("21474836480", 10), 21474836480);

   EXPECT_EQ(
      strtol_wrapper<s64>("9223372036854775807", 10),
      9223372036854775807ll
   ); // LLONG_MAX

   EXPECT_EQ(
      strtol_wrapper<s64>("-9223372036854775808", 10),
      -9223372036854775807ll - 1ll
   ); // LLONG_MIN
}

TEST(tilck_strtoul, basic_tests)
{
   EXPECT_EQ(strtol_wrapper<u32>("0", 10), 0u);
   EXPECT_EQ(strtol_wrapper<u32>("1234", 10), 1234u);
   EXPECT_EQ(strtol_wrapper<u32>("a", 16), 10u);
   EXPECT_EQ(strtol_wrapper<u32>("ff", 16), 255u);
   EXPECT_EQ(strtol_wrapper<u32>("02bbffdd", 16), 0x02bbffddu);
   EXPECT_EQ(strtol_wrapper<u32>("111001", 2), 0b111001u);
   EXPECT_EQ(strtol_wrapper<u32>("755", 8), 0755u);

   EXPECT_EQ(strtol_wrapper<u32>("-1", 10), 0u);
   EXPECT_EQ(strtol_wrapper<u32>("-134", 10), 0u);
}

TEST(tilck_strtol, errors)
{
   const char *str;
   const char *endptr;
   int error;
   int res;

   str = "abc";
   res = tilck_strtol32(str, &endptr, 10, &error);
   EXPECT_EQ(res, 0);
   EXPECT_EQ(endptr, str);
   EXPECT_EQ(error, -EINVAL);

   str = "2147483648"; // INT_MAX + 1
   res = tilck_strtol32(str, &endptr, 10, &error);
   EXPECT_EQ(res, 0);
   EXPECT_EQ(endptr, str);
   EXPECT_EQ(error, -ERANGE);

   str = "-2147483649"; // INT_MIN - 1
   res = tilck_strtol32(str, &endptr, 10, &error);
   EXPECT_EQ(res, 0);
   EXPECT_EQ(error, -ERANGE);
}
