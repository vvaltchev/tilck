
#pragma once

#include <commonDefs.h>

#if !defined(TEST) && !defined(KERNEL_TEST)
#define HEAP_BASE_ADDR (KERNEL_BASE_VADDR + 0x4000000) // BASE + 64 MB
#define HEAP_DATA_SIZE (512 * 1024 * 1024) // 512 MB
#else
extern void *kernel_heap_base;
#define HEAP_BASE_ADDR ((uintptr_t)kernel_heap_base)
#define HEAP_DATA_SIZE (64 * 1024 * 1024)
#endif

void initialize_kmalloc();
void *kmalloc(size_t size);
void kfree(void *ptr, size_t size);
