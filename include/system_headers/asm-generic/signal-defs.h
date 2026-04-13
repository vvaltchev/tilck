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

#ifdef __FreeBSD__
   /*
    * FreeBSD typedefs __sighandler_t as void(int) (a function type)
    * and defines SIG_DFL etc. as ((__sighandler_t *)...).  Tilck
    * follows the Linux convention where __sighandler_t is already a
    * pointer: void (*)(int).  Shadow the typedef with a macro and
    * redefine the constants so the types stay consistent.
    */
   #define __sighandler_t __signalfn_t *
   #undef SIG_DFL
   #undef SIG_IGN
   #undef SIG_ERR
   #define SIG_DFL ((__sighandler_t)0)
   #define SIG_IGN ((__sighandler_t)1)
   #define SIG_ERR ((__sighandler_t)-1)
#else
   typedef __signalfn_t *__sighandler_t;
#endif

/*
 * SIG_DFL, SIG_IGN, SIG_ERR, SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK are
 * provided by <signal.h> on all POSIX platforms (redefined above for
 * FreeBSD to match the Linux __sighandler_t convention).
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
