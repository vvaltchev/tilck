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
   ASSERT_EQ(arg_num, (int)args.size());
   args.push_back(arg);
}

TEST(cmdline, basic)
{
   EXPECT_EQ(parse_wrapper(""), (tvec{}));
   EXPECT_EQ(parse_wrapper("a"), (tvec{"a"}));
   EXPECT_EQ(parse_wrapper("a b c"), (tvec{"a", "b", "c"}));
}

TEST(cmdline, space_should_be_ignored)
{
   EXPECT_EQ(parse_wrapper(" "), (tvec{}));
   EXPECT_EQ(parse_wrapper("  "), (tvec{}));
   EXPECT_EQ(parse_wrapper("   a"), (tvec{"a"}));
   EXPECT_EQ(parse_wrapper("a   "), (tvec{"a"}));
   EXPECT_EQ(parse_wrapper("   a   "), (tvec{"a"}));
   EXPECT_EQ(parse_wrapper("   a    b    "), (tvec{"a", "b"}));
}

TEST(cmdline, arg_truncation)
{
   string long_arg;
   string truncated;
   string long_line;

   for (int i = 0; i <= MAX_CMD_ARG_LEN / 10; i++)
      long_arg += "0123456789";

   ASSERT_GT((int)long_arg.size(), MAX_CMD_ARG_LEN);

   truncated = long_arg;
   truncated.resize(MAX_CMD_ARG_LEN);

   EXPECT_EQ(parse_wrapper(long_arg.c_str()), (tvec{truncated}));

   long_line += "   a ";
   long_line += long_arg;

   EXPECT_EQ(parse_wrapper(long_line.c_str()), (tvec{"a", truncated}));

   long_line += "   ";
   EXPECT_EQ(parse_wrapper(long_line.c_str()), (tvec{"a", truncated}));

   long_line += "last";
   EXPECT_EQ(parse_wrapper(long_line.c_str()), (tvec{"a", truncated, "last"}));
}
