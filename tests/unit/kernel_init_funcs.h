/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

extern "C" {

   #include <tilck/common/basic_defs.h>

   void init_pageframe_allocator();
   void init_kmalloc();
   void init_worker_threads();
   void init_kmalloc_for_tests();
}

