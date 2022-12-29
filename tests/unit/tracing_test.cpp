/* SPDX-License-Identifier: BSD-2-Clause */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mocking.h"

using namespace testing;

extern "C" {
   #include <tilck/kernel/test/tracing.h>
}


class tracing_mock : public KernelSingleton {
public:
   MOCK_METHOD(int, handle_sys_trace_arg, (const char *arg), (override));
};

TEST(simple_wildcard_match, basic)
{
   ASSERT_TRUE(simple_wildcard_match("test string", "test string"));
   ASSERT_TRUE(simple_wildcard_match("test string", "test string*"));
   ASSERT_TRUE(simple_wildcard_match("test string", "tes? string"));
   ASSERT_TRUE(simple_wildcard_match("test string", "test *"));
   ASSERT_FALSE(simple_wildcard_match("test string", "?"));
   ASSERT_FALSE(simple_wildcard_match("test string", "test strin1"));
   ASSERT_FALSE(simple_wildcard_match("test string", "te*t"));
   ASSERT_FALSE(simple_wildcard_match("test string", "test string1"));
   ASSERT_TRUE(simple_wildcard_match("", ""));
}

TEST(simple_wildcard_match, questionmark)
{
   ASSERT_TRUE(simple_wildcard_match("abc", "a?c"));
   ASSERT_TRUE(simple_wildcard_match("abc", "??c"));
   ASSERT_FALSE(simple_wildcard_match("abc", "abc?"));
   ASSERT_TRUE(simple_wildcard_match("a,c", "a?c"));
   ASSERT_FALSE(simple_wildcard_match("abc", "abc??"));
   ASSERT_FALSE(simple_wildcard_match("abc", "?"));
   ASSERT_FALSE(simple_wildcard_match("abc", "?"));
   ASSERT_FALSE(simple_wildcard_match("abc", "??"));
   ASSERT_TRUE(simple_wildcard_match("abc", "???"));
   ASSERT_FALSE(simple_wildcard_match("abc", "????"));
   ASSERT_FALSE(simple_wildcard_match("", "?"));
}

TEST(simple_wildcard_match, star)
{
   ASSERT_TRUE(simple_wildcard_match("abc", "*"));
   ASSERT_TRUE(simple_wildcard_match("abc", "ab*"));
   ASSERT_TRUE(simple_wildcard_match("abc", "a*"));
   ASSERT_TRUE(simple_wildcard_match("abc", "abc*"));
   ASSERT_FALSE(simple_wildcard_match("abc", "*b"));
   ASSERT_FALSE(simple_wildcard_match("abc", "*abc"));
   ASSERT_TRUE(simple_wildcard_match("", "*"));
   ASSERT_FALSE(simple_wildcard_match("", "**"));
   ASSERT_FALSE(simple_wildcard_match("ab", "**"));
}

TEST(set_traced_syscalls_int, basic)
{
   tracing_mock mock;
   char tracing_str[TRACED_SYSCALLS_STR_LEN+1];
   bool traced_syscalls_mock[MAX_SYSCALLS] = {1};

   memset((void *) tracing_str, 'a', TRACED_SYSCALLS_STR_LEN);
   tracing_str[TRACED_SYSCALLS_STR_LEN] = '\0';
   traced_syscalls = traced_syscalls_mock;
   traced_syscalls_str = tracing_str;

   ASSERT_EQ(set_traced_syscalls_int(tracing_str), -ENAMETOOLONG);

   tracing_str[TRACED_SYSCALLS_STR_LEN-1] = '\0';
   ASSERT_EQ(set_traced_syscalls_int(tracing_str), -ENAMETOOLONG);

   EXPECT_CALL(mock, handle_sys_trace_arg(_))
      .WillOnce(Return(1));
   ASSERT_EQ(set_traced_syscalls_int(",,,"), 1);

   EXPECT_CALL(mock, handle_sys_trace_arg(_))
      .WillOnce(Return(1));
   ASSERT_EQ(set_traced_syscalls_int("abc"), 1);

   EXPECT_CALL(mock, handle_sys_trace_arg(_))
      .WillOnce(Return(0));
   ASSERT_EQ(set_traced_syscalls_int("abc"), 0);

   EXPECT_CALL(mock, handle_sys_trace_arg(_))
      .WillRepeatedly(Return(0));
   ASSERT_EQ(set_traced_syscalls_int("a, b, c"), 0);

   traced_syscalls = NULL;
   traced_syscalls_str = NULL;
}
