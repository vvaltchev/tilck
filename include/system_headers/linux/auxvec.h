/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shim for <linux/auxvec.h>.
 * On Linux, forward to the real header. On other platforms, provide the
 * auxiliary vector constants that Tilck needs.
 */

#pragma once

#ifdef __linux__
#include_next <linux/auxvec.h>
#else

#define AT_NULL    0
#define AT_PAGESZ  6

#endif /* !__linux__ */
