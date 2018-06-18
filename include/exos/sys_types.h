
#pragma once
#include <common/basic_defs.h>

#ifdef __EXOS_KERNEL__

typedef ssize_t off_t;

/* Used by stat64 */
typedef u64 dev_t;
typedef u64 ino_t;
typedef u32 mode_t;
typedef u32 nlink_t;
typedef u32 uid_t;
typedef u32 gid_t;
typedef u32 blksize_t;
typedef u64 blkcnt_t;
/* --- */

typedef uptr time_t;
typedef uptr suseconds_t;
typedef u32 clockid_t;

typedef struct {
   time_t      tv_sec;     /* seconds */
   suseconds_t tv_usec;    /* microseconds */
} timeval;

typedef struct {
   int tz_minuteswest;     /* minutes west of Greenwich */
   int tz_dsttime;         /* type of DST correction */
} timezone;

typedef struct {
   time_t tv_sec;          /* seconds */
   size_t tv_nsec;         /* nanoseconds */
} timespec;


typedef struct {

   dev_t     st_dev;         /* ID of device containing file */

   char padding1[8];

   mode_t    st_mode;        /* File type and mode */
   nlink_t   st_nlink;       /* Number of hard links */
   uid_t     st_uid;         /* User ID of owner */
   gid_t     st_gid;         /* Group ID of owner */
   dev_t     st_rdev;        /* Device ID (if special file) */

   char padding2[4];

   u64       st_size;        /* Total size, in bytes */
   blksize_t st_blksize;     /* Block size for filesystem I/O */
   blkcnt_t  st_blocks;      /* Number of 512B blocks allocated */

   timespec  st_atim;        /* Time of last access */
   timespec  st_mtim;        /* Time of last modification */
   timespec  st_ctim;        /* Time of last status change */

   ino_t     st_ino;         /* Inode number */

} stat64;

#else

/* DIRTY HACK to make the build of 'gtests' to pass */
// TODO: FIX IT.

#include <sys/types.h>
#include <sys/stat.h>

#endif // __EXOS_KERNEL__
