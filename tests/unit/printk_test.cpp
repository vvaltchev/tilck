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
   EXPECT_EQ(spk_wrapper("%X", 0xaab3), "AAB3");
   EXPECT_EQ(spk_wrapper("%o", 0755), "755");
   EXPECT_EQ(spk_wrapper("%c", 'a'), "a");
   EXPECT_EQ(spk_wrapper("%ld", (long)1234), "1234");
   EXPECT_EQ(spk_wrapper("%5d", 2), "    2");
   EXPECT_EQ(spk_wrapper("%05d", 2), "00002");
   EXPECT_EQ(spk_wrapper("%-5d", 2), "2    ");
   EXPECT_EQ(spk_wrapper("%5s", "abc"), "  abc");
   EXPECT_EQ(spk_wrapper("%-5s", "abc"), "abc  ");
   EXPECT_EQ(spk_wrapper("%5c", 'a'),  "    a");
   EXPECT_EQ(spk_wrapper("%-5c", 'a'), "a    ");
   EXPECT_EQ(spk_wrapper("%6X", 0xaab3), "  AAB3");
   EXPECT_EQ(spk_wrapper("%06X", 0xaab3), "00AAB3");

   EXPECT_EQ(spk_wrapper("%lld", 9223372036854775807ll), "9223372036854775807");
   EXPECT_EQ(spk_wrapper("%llx", 0xaabbccddeeffll), "aabbccddeeff");

   /* Multi-param string */
   EXPECT_EQ(spk_wrapper("p[%s] = %d", "opt1", 1234), "p[opt1] = 1234");
}

TEST(printk, rare)
{
   /* String precision */
   EXPECT_EQ(spk_wrapper("%.3s", "abcdef"), "abc");

   /* Precision for integers treated as lpad */
   EXPECT_EQ(spk_wrapper("%.4d", 1), "0001");

   /* Same as long long */
   EXPECT_EQ(spk_wrapper("%Lx", 0xaabbccddeeffll), "aabbccddeeff");
   EXPECT_EQ(spk_wrapper("%qx", 0xaabbccddeeffll), "aabbccddeeff");
   EXPECT_EQ(spk_wrapper("%jx", 0xaabbccddeeffll), "aabbccddeeff");

   /* Half (short) and half half (char) */
   EXPECT_EQ(spk_wrapper("%x", (signed char)-1), "ffffffff");
   EXPECT_EQ(spk_wrapper("%hx", (signed char)-1), "ffff");
   EXPECT_EQ(spk_wrapper("%hhx", (signed char)-1), "ff");
   EXPECT_EQ(spk_wrapper("%hd", (short)-1234), "-1234");
   EXPECT_EQ(spk_wrapper("%hhd", (signed char)-123), "-123");

   /* Corner cases */
   EXPECT_EQ(spk_wrapper("%05c", 'a'),  "    a");  /* zero-pad is ignored */
}

TEST(printk, varparam)
{
   EXPECT_EQ(spk_wrapper("%.*s", 3, "abcdef"), "abc");   /* precision */
   EXPECT_EQ(spk_wrapper("%.*s", 0, "abcdef"), "");      /* precision=0 */
   EXPECT_EQ(spk_wrapper("%*s", 6, "abc"), "   abc");    /* left-padding */
   EXPECT_EQ(spk_wrapper("%-*s", 6, "abc"), "abc   ");   /* right-padding */
   EXPECT_EQ(spk_wrapper("%0*d", 5, 23), "00023");       /* zero-pad */
   EXPECT_EQ(spk_wrapper("%*d", 5, 23), "   23");        /* numeric left-pad */
   EXPECT_EQ(spk_wrapper("%-*d", 5, 23), "23   ");       /* numeric right-pad */
   EXPECT_EQ(spk_wrapper("%*d", -5, 23), "23   ");       /* negative num lpad */
   EXPECT_EQ(spk_wrapper("%-*d", -5, 23), "23   ");      /* negative num rpad */

}

TEST(printk, hashsign)
{
   EXPECT_EQ(spk_wrapper("%#x",   0x123), "0x123");    // Just prepend "0x"
   EXPECT_EQ(spk_wrapper("%#08x", 0x123), "0x000123"); // "0x" counted in lpad
   EXPECT_EQ(spk_wrapper("%#8x",  0x123), "   0x123"); // "0x" counted in lpad
   EXPECT_EQ(spk_wrapper("%#-8x", 0x123), "0x123   "); // "0x" counted in rpad
   EXPECT_EQ(spk_wrapper("%#08X", 0xabc), "0x000ABC"); // "0x" counted in lpad

   EXPECT_EQ(spk_wrapper("%#o",   0755), "0755");      // Just prepend "0"
   EXPECT_EQ(spk_wrapper("%#08o", 0755), "00000755");  // "0" counted in lpad
   EXPECT_EQ(spk_wrapper("%#8o",  0755), "    0755");  // "0" counted in lpad
   EXPECT_EQ(spk_wrapper("%#-8o", 0755), "0755    ");  // "0" counted in rpad

   /* Corner cases */
   EXPECT_EQ(spk_wrapper("%##x",  0x123), "0x123");
   EXPECT_EQ(spk_wrapper("%###x", 0x123), "0x123");
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
   EXPECT_EQ(spk_wrapper("%h"), "");
   EXPECT_EQ(spk_wrapper("%hh"), "");
}

TEST(printk, incomplete_seq)
{
   EXPECT_EQ(spk_wrapper("%z, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%l, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%ll, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%0, hello"), "%0, hello");
   EXPECT_EQ(spk_wrapper("%5, hello"), "%5, hello");
   EXPECT_EQ(spk_wrapper("%-5, hello"), "%-5, hello");
   EXPECT_EQ(spk_wrapper("%-0, hello"), "%, hello"); /* should be: %-, hello */
   EXPECT_EQ(spk_wrapper("%h, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%hh, hello"), "%, hello");
   EXPECT_EQ(spk_wrapper("%#, hello"), "%#, hello"); /* note: %# is kept */
}

TEST(printk, invalid_seq)
{
   EXPECT_EQ(spk_wrapper("%w", 123), "%w");
   EXPECT_EQ(spk_wrapper("%lll", 123ll), "%l");
   EXPECT_EQ(spk_wrapper("%#"), "%#");
   EXPECT_EQ(spk_wrapper("%##"), "%#");
   EXPECT_EQ(spk_wrapper("%###"), "%#");
   EXPECT_EQ(spk_wrapper("%l#d"), "%#d");
   EXPECT_EQ(spk_wrapper("%lh"), "%h");
   EXPECT_EQ(spk_wrapper("%hl"), "%l");
   EXPECT_EQ(spk_wrapper("%hld"), "%ld");
   EXPECT_EQ(spk_wrapper("%lhd"), "%hd");
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

TEST(printk, ptrdiff_t)
{
   EXPECT_EQ(spk_wrapper("%td", (size_t)1234), "1234");
   EXPECT_EQ(spk_wrapper("%tu", (size_t)123), "123");
   EXPECT_EQ(spk_wrapper("%tx", (size_t)0xaab3), "aab3");

#if NBITS == 64
   EXPECT_EQ(spk_wrapper("%tu",(size_t)9223372036854775ll),"9223372036854775");
#endif
}
