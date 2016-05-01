
#pragma once

#include <commonDefs.h>


void init_physical_page_allocator();
void *alloc_phys_page();
void free_phys_page();

