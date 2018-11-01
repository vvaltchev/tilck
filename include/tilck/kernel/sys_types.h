/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#include <features.h>   // system header

#if defined(__GLIBC__) && !defined(__USE_LARGEFILE64)
   #define __USE_LARGEFILE64
#endif

#include <sys/types.h>  // system header
#include <sys/time.h>   // system header
#include <sys/uio.h>    // system header

#ifndef __GLIBC__
   #define stat stat64
#endif

#include <sys/stat.h>   // system header

#ifndef __GLIBC__
   #undef stat
#endif

#include <unistd.h>     // system header

/* From the man page of getdents64() */
struct linux_dirent64 {
   s64            d_ino;    /* 64-bit inode number */
   s64            d_off;    /* 64-bit offset to next structure */
   unsigned short d_reclen; /* Size of this dirent */
   unsigned char  d_type;   /* File type */
   char           d_name[]; /* Filename (null-terminated) */
};
