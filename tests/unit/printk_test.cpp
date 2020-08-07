/* SPDX-License-Identifier: BSD-2-Clause */

#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <gtest/gtest.h>

using namespace std;

extern "C" {
   #include <tilck/common/printk.h>
}

template <typename ...Args>
string spk_wrapper(const char *fmt, Args&& ...args)
{
   char buf[64];
   snprintk(buf, sizeof(buf), fmt, args...);
   return buf;
}

TEST(printk, basic)
{
   EXPECT_EQ(spk_wrapper("%%"), "%");
   EXPECT_EQ(spk_wrapper("%d", 1234), "1234");
   EXPECT_EQ(spk_wrapper("%d", -2), "-2");
   EXPECT_EQ(spk_wrapper("%i", -123), "-123");
   EXPECT_EQ(spk_wrapper("%x", 0xaab3), "aab3");
   EXPECT_EQ(spk_wrapper("%o", 0755), "755");
   EXPECT_EQ(spk_wrapper("%ld", (long)1234), "1234");
   EXPECT_EQ(spk_wrapper("%5d", 2), "    2");
   EXPECT_EQ(spk_wrapper("%05d", 2), "00002");
   EXPECT_EQ(spk_wrapper("%-5d", 2), "2    ");
   EXPECT_EQ(spk_wrapper("%5s", "abc"), "  abc");
   EXPECT_EQ(spk_wrapper("%-5s", "abc"), "abc  ");

   EXPECT_EQ(spk_wrapper("%lld", 9223372036854775807ll), "9223372036854775807");
   EXPECT_EQ(spk_wrapper("%llx", 0xaabbccddeeffll), "aabbccddeeff");
}

TEST(printk, rare)
{
   /* Same as long long */
   EXPECT_EQ(spk_wrapper("%Lx", 0xaabbccddeeffll), "aabbccddeeff");
   EXPECT_EQ(spk_wrapper("%qx", 0xaabbccddeeffll), "aabbccddeeff");
   EXPECT_EQ(spk_wrapper("%jx", 0xaabbccddeeffll), "aabbccddeeff");
}

TEST(printk, hashsign)
{
   EXPECT_EQ(spk_wrapper("%#x",   0x123), "0x123");    // Just prepend "0x"
   EXPECT_EQ(spk_wrapper("%#08x", 0x123), "0x000123"); // "0x" counted in lpad
   EXPECT_EQ(spk_wrapper("%#8x",  0x123), "   0x123"); // "0x" counted in lpad
   EXPECT_EQ(spk_wrapper("%#-8x", 0x123), "0x123   "); // "0x" counted in rpad

   EXPECT_EQ(spk_wrapper("%#o",   0755), "0755");      // Just prepend "0"
   EXPECT_EQ(spk_wrapper("%#08o", 0755), "00000755");  // "0" counted in lpad
   EXPECT_EQ(spk_wrapper("%#8o",  0755), "    0755");  // "0" counted in lpad
   EXPECT_EQ(spk_wrapper("%#-8o", 0755), "0755    ");  // "0" counted in rpad
}

TEST(printk, truncated_seq)
{
   EXPECT_EQ(spk_wrapper("%z"), "");
   EXPECT_EQ(spk_wrapper("%l"), "");
   EXPECT_EQ(spk_wrapper("%ll"), "");
   EXPECT_EQ(spk_wrapper("%0"), "");
   EXPECT_EQ(spk_wrapper("%5"), "");
   EXPECT_EQ(spk_wrapper("%5"), "");
   EXPECT_EQ(spk_wrapper("%-5"), "");
   EXPECT_EQ(spk_wrapper("%#"), "");
}

TEST(printk, incomplete_seq)
{
   EXPECT_EQ(spk_wrapper("%z, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%l, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%ll, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%0, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%5, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%5, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%-5, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%#, hello"), "%#, hello");
}

TEST(printk, invalid_seq)
{
   EXPECT_EQ(spk_wrapper("%w", 123), "%w");
   EXPECT_EQ(spk_wrapper("%lll", 123ll), "%l");
}

TEST(printk, pointers)
{
   if (NBITS == 32) {

      EXPECT_EQ(spk_wrapper("%p", TO_PTR(0xc0aabbc0)), "0xc0aabbc0");
      EXPECT_EQ(
         spk_wrapper("%20p", TO_PTR(0xc0aabbc0)), "            0xc0aabbc0"
      );
      EXPECT_EQ(
         spk_wrapper("%-20p", TO_PTR(0xc0aabbc0)), "0xc0aabbc0            "
      );

   } else {

      EXPECT_EQ(spk_wrapper("%p", TO_PTR(0xc0aabbc0)), "0x00000000c0aabbc0");
      EXPECT_EQ(
         spk_wrapper("%20p", TO_PTR(0xc0aabbc0)), "    0x00000000c0aabbc0"
      );
      EXPECT_EQ(
         spk_wrapper("%-20p", TO_PTR(0xc0aabbc0)), "0x00000000c0aabbc0    "
      );
   }
}

TEST(printk, size_t)
{
   EXPECT_EQ(spk_wrapper("%zd", (size_t)1234), "1234");
   EXPECT_EQ(spk_wrapper("%zu", (size_t)123), "123");
   EXPECT_EQ(spk_wrapper("%zx", (size_t)0xaab3), "aab3");

#if NBITS == 64
   EXPECT_EQ(spk_wrapper("%zu",(size_t)9223372036854775ll),"9223372036854775");
#endif
}
