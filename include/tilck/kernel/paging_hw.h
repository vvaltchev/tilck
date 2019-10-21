/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal.h>

static ALWAYS_INLINE void set_curr_pdir(pdir_t *pdir)
{
   __set_curr_pdir(KERNEL_VA_TO_PA(pdir));
}

static ALWAYS_INLINE pdir_t *get_curr_pdir()
{
   return (pdir_t *)KERNEL_PA_TO_VA(__get_curr_pdir());
}

/*
 * Tilck's entry point is in `_start` where the so-called "original"
 * page directory is set, using the `page_size_buf` as buffer. The original
 * page directory just linearly maps the first 4 MB of the physical memory to
 * KERNEL_BASE_VA. This function returns true if we're still using that page
 * directory (-> init_paging() has not been called yet).
 */
static ALWAYS_INLINE bool still_using_orig_pdir(void)
{
#ifndef UNIT_TEST_ENVIRONMENT
   return get_curr_pdir() == (void *)page_size_buf;
#else
   return false;
#endif
}
