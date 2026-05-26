/* SPDX-License-Identifier: BSD-2-Clause */

#define MY_NULL NULL

void foo(int *p)
{
   if (p == nullptr)
      return;

   int *q = nullptr;
}
