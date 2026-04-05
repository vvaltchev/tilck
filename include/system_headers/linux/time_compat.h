/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Linux-specific clock IDs not available on all POSIX platforms.
 * Include this header AFTER <time.h>.
 */

#pragma once

#ifndef CLOCK_REALTIME_COARSE
#define CLOCK_REALTIME_COARSE    5
#endif

/*
 * Only define CLOCK_MONOTONIC_COARSE when it won't collide with
 * another clock ID.  Some platforms (e.g. macOS) map CLOCK_MONOTONIC
 * to value 6, which is the same numeric value Linux uses for
 * CLOCK_MONOTONIC_COARSE.  We cannot portably compare the value at
 * preprocessing time (it may be an enum behind a macro), so just
 * skip the definition when CLOCK_MONOTONIC is a platform-provided
 * macro — on Linux, CLOCK_MONOTONIC_COARSE is already defined by
 * the system headers.
 */
#if !defined(CLOCK_MONOTONIC_COARSE) && !defined(CLOCK_MONOTONIC)
   #define CLOCK_MONOTONIC_COARSE   6
#endif
