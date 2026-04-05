/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shim for <asm-generic/signal-defs.h>.
 * On Linux, forward to the real header. On other platforms, provide the
 * signal-related types and constants that Tilck needs.
 */

#pragma once

#ifdef __linux__
#include_next <asm-generic/signal-defs.h>
#else

#include <signal.h>

typedef void __signalfn_t(int);
typedef __signalfn_t *__sighandler_t;

/*
 * SIG_DFL, SIG_IGN, SIG_ERR, SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK are
 * provided by <signal.h> on all POSIX platforms.
 */

/* Linux value: 64 signals (needed for kernel signal mask sizing) */
#ifndef _NSIG
#define _NSIG 64
#endif

/* SIGPOLL is Linux-specific; POSIX platforms typically only have SIGIO */
#ifndef SIGPOLL
   #ifdef SIGIO
      #define SIGPOLL SIGIO
   #else
      #define SIGPOLL 29
   #endif
#endif

/*
 * SIGPWR (power failure) is Linux-specific.  On Linux it is 30, but
 * some platforms use 30 for SIGUSR1, so pick a high unused slot to
 * avoid designated-initializer collisions in signal name tables.
 */
#ifndef SIGPWR
   #define SIGPWR 35
#endif

#endif /* !__linux__ */
