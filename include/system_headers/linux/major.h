/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shim for <linux/major.h>.
 * On Linux, forward to the real header. On other platforms, provide the
 * device major numbers that Tilck needs.
 */

#pragma once

#ifdef __linux__
#include_next <linux/major.h>
#else

#define MEM_MAJOR      1
#define TTY_MAJOR      4
#define TTYAUX_MAJOR   5
#define FB_MAJOR      29
#define MISC_MAJOR    10

#endif /* !__linux__ */
