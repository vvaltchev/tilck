/* SPDX-License-Identifier: BSD-2-Clause */

#include <iostream>
#include <random>
#include <limits>
#include <ctime>
#include <gtest/gtest.h>

extern "C" {
   #include <tilck/common/basic_defs.h>
   #include <tilck/common/datetime.h>
}

using namespace std;
using namespace testing;

static void biconv_test(int64_t min, int64_t max)
{
   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);
   uniform_int_distribution<int64_t> dist(min, max);

   cout << "[ INFO     ] random seed: " << seed << endl;

   for (int i = 0; i < 100000; i++) {

      const int64_t t = dist(e);
      datetime d;

      const int rc = timestamp_to_datetime(t, &d);
      ASSERT_EQ(rc, 0);

      const int64_t convT = datetime_to_timestamp(d);
      ASSERT_EQ(convT, t);
   }
}

TEST(datetime, biconv_32bit)
{
   biconv_test(numeric_limits<int>::min(), numeric_limits<int>::max());
}

TEST(datetime, biconv_ext)
{
   biconv_test(
      -59011459200ull,     /* Friday, January 1, 0100 00:00:00   */
       253402300799ull     /* Friday, December 31, 9999 23:59:59 */
   );
}


TEST(datetime, timestamp_to_datetime)
{
   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);
   uniform_int_distribution<> dist(
      numeric_limits<int>::min(),
      numeric_limits<int>::max()
   );

   cout << "[ INFO     ] random seed: " << seed << endl;

   for (int i = 0; i < 30000; i++) {

      datetime d;
      struct tm tm;
      const time_t t = dist(e);

      const int rc = timestamp_to_datetime((int64_t)t, &d);
      ASSERT_EQ(rc, 0);

      gmtime_r(&t, &tm);

      ASSERT_EQ(tm.tm_sec, d.sec) << "T: " << t;
      ASSERT_EQ(tm.tm_min, d.min) << "T: " << t;
      ASSERT_EQ(tm.tm_hour, d.hour) << "T: " << t;
      // ASSERT_EQ(tm.tm_wday, d.weekday) << "T: " << t;
      ASSERT_EQ(tm.tm_mday, d.day) << "T: " << t;
      ASSERT_EQ(tm.tm_mon+1, d.month) << "T: " << t;
      ASSERT_EQ(tm.tm_year+1900, d.year) << "T: " << t;
   }
}
