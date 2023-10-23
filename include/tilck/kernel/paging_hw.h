/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal.h>

static ALWAYS_INLINE void set_curr_pdir(pdir_t *pdir)
{
   __set_curr_pdir(LIN_VA_TO_PA(pdir));
}

static ALWAYS_INLINE pdir_t *get_curr_pdir()
{
   return PA_TO_LIN_VA(__get_curr_pdir());
}

/*
 * Tilck's entry point is in `_start` where the so-called "original"
 * page directory is set, using the `page_size_buf` as buffer. The original
 * page directory just linearly maps the first 8 MB of the physical memory to
 * BASE_VA. This function returns true if we're still using that page
 * directory (-> early_init_paging() has not been called yet).
 */
static ALWAYS_INLINE bool still_using_orig_pdir(void)
{
#ifndef UNIT_TEST_ENVIRONMENT
   /*
    * Use the lower-level __get_curr_pdir() function and compare the pdirs using
    * their physical address, in order to handle the case where KRN32_LIN_VADDR
    * is disabled.
    */
   return __get_curr_pdir() == KERNEL_VA_TO_PA(page_size_buf);
#else
   return false;
#endif
}
