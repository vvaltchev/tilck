/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shim for <sys/prctl.h>.
 * On Linux, forward to the real header. On other platforms, provide the
 * prctl option constants that Tilck needs.
 */

#pragma once

#ifdef __linux__
#include_next <sys/prctl.h>
#else

#define PR_SET_NAME    15
#define PR_GET_NAME    16

#endif /* !__linux__ */
