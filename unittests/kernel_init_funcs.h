/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>

extern "C" {
   void init_pageframe_allocator();
   void init_kmalloc();
   void init_tasklets();
   void init_kmalloc_for_tests();
}

