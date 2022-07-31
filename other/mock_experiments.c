/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

bool experiment_bar(void) {
   return true;
}

int experiment_foo(int n) {

   if (!experiment_bar())
      return -1;

   return n * 10;
}
