/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shim for <features.h>.
 * On Linux (glibc), forward to the real header. On other platforms,
 * provide an empty stub — the only Tilck usage is to check __GLIBC__,
 * which will not be defined on non-glibc systems anyway.
 */

#pragma once

#ifdef __linux__
#include_next <features.h>
#endif
