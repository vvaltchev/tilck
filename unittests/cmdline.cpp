/* SPDX-License-Identifier: BSD-2-Clause */

#include <iostream>
#include <vector>
#include <string>
#include <gtest/gtest.h>

using namespace std;

extern "C" {
   #include <tilck/kernel/cmdline.h>
   void use_kernel_arg(int arg_num, const char *arg);
}

using tvec = vector<string>;
static tvec args;

static const tvec& parse_wrapper(const char *cmdline)
{
   args.clear();
   parse_kernel_cmdline(cmdline);
   return args;
}

void use_kernel_arg(int arg_num, const char *arg)
{
   args.push_back(arg);
}

TEST(cmdline, basic)
{
   EXPECT_EQ(parse_wrapper(""), (tvec{}));
   EXPECT_EQ(parse_wrapper("a"), (tvec{"a"}));
   EXPECT_EQ(parse_wrapper("a b c"), (tvec{"a", "b", "c"}));
}

// TEST(cmdline, space_should_be_ignored)
// {
//    EXPECT_EQ(parse_wrapper(" "), (tvec{}));
//    EXPECT_EQ(parse_wrapper("  "), (tvec{}));
// }
