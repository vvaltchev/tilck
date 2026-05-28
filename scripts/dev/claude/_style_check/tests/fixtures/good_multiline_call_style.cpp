/* SPDX-License-Identifier: BSD-2-Clause */

#include <chrono>

using namespace std::chrono;

extern unsigned long g_tot;
extern int sum4(int a, int b, int c, int d);

/* Style 1 (compliant): first arg on the ( line, continuation aligned. */
int style1_caller(void)
{
   return sum4(111, 222,
               333, 444);
}

/* Style 2 (compliant): ( ends the first line, args at +3 indent,
 * ); on its own line aligned with the wrapping statement. */
int style2_caller(void)
{
   return sum4(
      111,
      222,
      333,
      444
   );
}

void foo(void)
{
   /* Single-line call: never wraps, must not trigger anything. */
   const auto start = steady_clock::now();
   const auto end = steady_clock::now();
   g_tot += (unsigned long)duration_cast<nanoseconds>(end - start).count();
}
