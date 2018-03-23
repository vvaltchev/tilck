
#pragma once

#include <common/basic_defs.h>

extern "C" {
   void init_pageframe_allocator();
   void initialize_kmalloc();
   void initialize_tasklets();
   void initialize_kmalloc_for_tests();
}

