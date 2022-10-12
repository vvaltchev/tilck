/* SPDX-License-Identifier: BSD-2-Clause */

#include <gtest/gtest.h>

using namespace testing;

extern "C" {
   #include <tilck/common/string_util.h>
}

TEST(strstr, basic)
{
   const char haystack[] = "haystack", needle[] = "needle", \
   hay[] = "hay", empty[] = "";
   ASSERT_STREQ(tilck_strstr(empty, needle), (char *) NULL);
   ASSERT_STREQ(tilck_strstr(haystack, needle), (char *) NULL);
   ASSERT_STREQ(tilck_strstr(haystack, hay), haystack);
}

TEST(strncpy, basic)
{
   char dest[4], src[] = "a";
   tilck_strncpy(dest, src, 2);
   ASSERT_STREQ(dest, "a");
}

TEST(strncat, basic)
{
   char dest[6] = "abc", src[] = "d";
   tilck_strncat(dest, src, 4);
   ASSERT_STREQ(dest, "abcd");
}

TEST(isxdigit, basic)
{
   ASSERT_EQ(tilck_isxdigit(48), true);
   ASSERT_EQ(tilck_isxdigit(58), false);
   ASSERT_EQ(tilck_isxdigit(71), false);
   ASSERT_EQ(tilck_isxdigit(127), false);
   ASSERT_EQ(tilck_isxdigit(128), false);
}

TEST(isspace, basic)
{
   ASSERT_EQ(tilck_isspace(' '), true);
   ASSERT_EQ(tilck_isspace('\n'), true);
   ASSERT_EQ(tilck_isspace('a'), false);
}

TEST(str_reverse, basic)
{
   char empty[] = "", short_string[] = "abc";
   str_reverse(empty, 0);
   ASSERT_STREQ("", empty);
   str_reverse(short_string, 3);
   ASSERT_STREQ("cba", short_string);
}
