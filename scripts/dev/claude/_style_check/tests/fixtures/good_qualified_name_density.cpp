/* SPDX-License-Identifier: BSD-2-Clause */

#include <chrono>
#include <vector>

using namespace std::chrono;
namespace stdc = std;

extern unsigned long g_tot;

void single_qualifier(void)
{
   /* Each statement has at most one `::` qualifier chain --
    * no gradient penalty. */
   auto v = stdc::vector<int>();
   (void)v;

   const auto start = steady_clock::now();
   const auto end = steady_clock::now();
   g_tot += (unsigned long)duration_cast<nanoseconds>(end - start).count();
}
