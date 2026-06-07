/* SPDX-License-Identifier: BSD-2-Clause */

#include <chrono>
#include <vector>

extern unsigned long g_tot;

void foo(void)
{
   /* 1 :: -- no penalty */
   auto a = std::vector<int>();
   (void)a;

   /* 2 :: -- COST_MINOR (0.10) */
   auto t = std::chrono::steady_clock();
   (void)t;

   /* 4 :: -- capped at COST_MODERATE (0.35) */
   const auto start = std::chrono::steady_clock::now();
   const auto end = std::chrono::steady_clock::now();
   g_tot += (unsigned long)std::chrono::duration_cast<std::chrono::nanoseconds>(
               end - start).count();
}
