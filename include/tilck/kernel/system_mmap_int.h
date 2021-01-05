/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Expose the internal data variables of system memory's map.
 *
 * WARNING
 * ---------
 * This *internal* header is supposed to be used in the very few places where
 * actually updating system's memory map is allowed. For example:
 *
 *    - in set_framebuffer_info_from_mbi(),
 *      during the initialization of the framebuffer console
 *
 *    - in arch_add_initial_mem_regions(),
 *      during system_mmap_set()
 *
 *    - in arch_add_final_mem_regions(),
 *      during system_mmap_set()
 *
 *    - in the system map's unit tests
 *
 * Outside of kernel's initialization, system's memory map is assumed to be
 * immutable.
 */

#pragma once
#include <tilck/kernel/system_mmap.h>

extern struct mem_region mem_regions[MAX_MEM_REGIONS];
extern int mem_regions_count;
void append_mem_region(struct mem_region r);
