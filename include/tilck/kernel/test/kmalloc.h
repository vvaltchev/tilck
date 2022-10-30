/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/kmalloc.h>

#ifdef UNIT_TEST_ENVIRONMENT
extern bool kmalloc_initialized;
extern struct kmalloc_heap first_heap_struct;
extern struct kmalloc_heap *heaps[KMALLOC_HEAPS_COUNT];
extern int used_heaps;
extern size_t max_tot_heap_mem_free;
#endif
