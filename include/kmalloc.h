
#pragma once

#include <commonDefs.h>

#ifndef TEST
#define HEAP_BASE_ADDR (KERNEL_BASE_VADDR + 0x4000000) // BASE + 64 MB
#else
uintptr_t test_get_heap_base();
#define HEAP_BASE_ADDR (test_get_heap_base())
#endif

void initialize_kmalloc();
void *kmalloc(size_t size);
void kfree(void *ptr, size_t size);
