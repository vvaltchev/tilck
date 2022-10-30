/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/system_mmap.h>

STATIC void fix_mem_regions(void);
STATIC void align_mem_regions_to_page_boundary(void);
STATIC void sort_mem_regions(void);
STATIC void merge_adj_mem_regions(void);
STATIC void handle_overlapping_regions(void);

extern struct mem_region mem_regions[MAX_MEM_REGIONS];
extern int mem_regions_count;
