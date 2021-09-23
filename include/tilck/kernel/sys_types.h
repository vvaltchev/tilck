/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/signal.h>

#include <features.h>   // system header

#if defined(__GLIBC__) && !defined(__USE_LARGEFILE64)
   #define __USE_LARGEFILE64
#endif

#include <sys/types.h>  // system header
#include <sys/time.h>   // system header
#include <sys/times.h>  // system header
#include <sys/uio.h>    // system header
#include <sys/select.h> // system header
#include <time.h>       // system header
#include <poll.h>       // system header
#include <utime.h>      // system header
#include <sys/stat.h>   // system header
#include <unistd.h>       // system header
#include <sys/utsname.h>  // system header
#include <sys/stat.h>     // system header
#include <fcntl.h>        // system header

#define MAX_SYSCALLS 500

typedef u64 tilck_ino_t;

/* From the man page of getdents64() */
struct linux_dirent64 {
   u64            d_ino;    /* 64-bit inode number */
   u64            d_off;    /* 64-bit offset to next structure */
   unsigned short d_reclen; /* Size of this dirent */
   unsigned char  d_type;   /* File type */
   char           d_name[]; /* Filename (null-terminated) */
};

struct k_sigaction {

   union {
      void (*handler)(int);
      void (*sigact_handler)(int, void * /* siginfo */, void *);
   };

   ulong sa_flags;
   void (*restorer)(void);
   ulong sa_mask[K_SIGACTION_MASK_WORDS];
};

/*
 * Classic timeval struct, with pointer-size tv_sec and tv_usec.
 * Suffers from the Y2038 bug on 32-bit systems.
 */
struct k_timeval {

   long tv_sec;
   long tv_usec;
};

/*
 * Classic utimbuf using pointer-size as time_t.
 * Suffers from the Y2038 bug on 32-bit systems.
 */
struct k_utimbuf {

   long actime;
   long modtime;
};

struct k_rusage {

   struct k_timeval ru_utime;
   struct k_timeval ru_stime;

/* linux extentions */
   long ru_maxrss;
   long ru_ixrss;
   long ru_idrss;
   long ru_isrss;
   long ru_minflt;
   long ru_majflt;
   long ru_nswap;
   long ru_inblock;
   long ru_oublock;
   long ru_msgsnd;
   long ru_msgrcv;
   long ru_nsignals;
   long ru_nvcsw;
   long ru_nivcsw;

   long reserved[16];
};

#ifdef BITS32
   STATIC_ASSERT(sizeof(struct k_rusage) == 136);
#endif

/*
 * Classic (old) timespec. Suffers from the Y2038 bug on ALL systems.
 */
struct k_timespec32 {

   s32 tv_sec;
   long tv_nsec;
};

/*
 * Modern timespec struct.
 */
struct k_timespec64 {

   s64 tv_sec;
   long tv_nsec;
};

#ifdef BITS32

/*
 * Classic stat64 struct for 32-bit systems.
 *
 * It replaced the older "struct stat" which had no forced 64-bit fields on
 * 32-bit systems. For example, st_dev was just a long. But the story is not
 * over: before "struct stat" there was a version of that struct now called
 * "__old_kernel_stat" in Linux that used short for many fields like st_dev,
 * st_ino, st_uid etc.
 *
 * Suffers from the Y2038 bug because of the k_timespec32 fields. Workaround
 * used by libmusl? Using statx() and struct statx.
 */
struct k_stat64 {
   u64 st_dev;
   u32 __st_dev_padding;
   ulong __st_ino_truncated;
   u32 st_mode;
   u32 st_nlink;
   ulong st_uid;
   ulong st_gid;
   u64 st_rdev;
   u32 __st_rdev_padding;
   s64 st_size;
   ulong st_blksize;
   s64 st_blocks;
   struct k_timespec32 st_atim;
   struct k_timespec32 st_mtim;
   struct k_timespec32 st_ctim;
   u64 st_ino;
};

#else

/*
 * Modern struct stat for 64-bit systems.
 *
 * Note: it's called simply "stat" in the Linux kernel.
 */
struct k_stat64 {

   ulong st_dev;
   ulong st_ino;
   ulong st_nlink;

   u32 st_mode;
   u32 st_uid;
   u32 st_gid;
   u32 __pad0;
   ulong	st_rdev;
   long st_size;
   long st_blksize;
   long st_blocks;

   struct k_timespec64 st_atim;
   struct k_timespec64 st_mtim;
   struct k_timespec64 st_ctim;

   long __unused[3];
};

#endif

#ifndef O_DIRECTORY
   #define O_DIRECTORY __O_DIRECTORY
#endif

#ifndef O_TMPFILE
   #define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)
#endif

#ifndef O_DIRECT
   #define O_DIRECT __O_DIRECT
#endif

#ifndef O_NOATIME
   #define O_NOATIME __O_NOATIME
#endif

#ifndef O_PATH
   #define O_PATH __O_PATH
#endif

#define FCNTL_CHANGEABLE_FL (         \
   O_APPEND      |                    \
   O_ASYNC       |                    \
   O_DIRECT      |                    \
   O_NOATIME     |                    \
   O_NONBLOCK                         \
)

#define EXITCODE(ret, sig)    ((ret) << 8 | (sig))
#define STOPCODE(sig)          ((sig) << 8 | 0x7f)
#define CONTINUED                           0xffff
#define COREFLAG                              0x80
