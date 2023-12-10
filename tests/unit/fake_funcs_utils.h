/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

extern "C" {
   void initialize_test_kernel_heap();
   extern bool mock_kmalloc;
   extern bool suppress_printk;
}
