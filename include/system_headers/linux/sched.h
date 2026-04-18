/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shim for <linux/sched.h>.
 * On Linux, forward to the real header. On other platforms, provide the
 * clone flags that Tilck needs.
 */

#pragma once

#ifdef __linux__
#include_next <linux/sched.h>
#else

#define CLONE_VM     0x00000100
#define CLONE_VFORK  0x00004000

#endif /* !__linux__ */
