/* SPDX-License-Identifier: BSD-2-Clause */

#include <chrono>

extern unsigned long g_tot;

void foo(void)
{
   /* Style 3 (violation): args on one indented line, ) glued to last
    * arg, not on its own line. Same pattern as in
    * tests/unit/avl_bintree.cpp before the using-namespace fix. */
   const auto start = std::chrono::steady_clock::now();
   const auto end = std::chrono::steady_clock::now();
   g_tot += (unsigned long)std::chrono::duration_cast<std::chrono::nanoseconds>(
               end - start).count();
}
