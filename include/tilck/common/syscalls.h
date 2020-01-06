/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <sys/syscall.h>         // system header

#define TILCK_CMD_SYSCALL    499

enum tilck_cmd {

   TILCK_CMD_RUN_SELFTEST        = 0,
   TILCK_CMD_GCOV_GET_NUM_FILES  = 1,
   TILCK_CMD_GCOV_FILE_INFO      = 2,
   TILCK_CMD_GCOV_GET_FILE       = 3,
   TILCK_CMD_QEMU_POWEROFF       = 4,
   TILCK_CMD_SET_SAT_ENABLED     = 5,
   TILCK_CMD_DEBUG_PANEL         = 6,

   /* Number of elements in the enum */
   TILCK_CMD_COUNT               = 7,
};

#if defined(__x86_64__)

   #define STAT_SYSCALL_N      SYS_stat
   #define LSTAT_SYSCALL_N     SYS_lstat
   #define FSTAT_SYSCALL_N     SYS_fstat
   #define FCNTL_SYSCALL_N     SYS_fcntl
   #define MMAP_SYSCALL_N      SYS_mmap

#elif defined(__i386__)

   #define STAT_SYSCALL_N      SYS_stat64
   #define LSTAT_SYSCALL_N     SYS_lstat64
   #define FSTAT_SYSCALL_N     SYS_fstat64
   #define FCNTL_SYSCALL_N     SYS_fcntl64
   #define MMAP_SYSCALL_N        192

   #undef SYS_getuid
   #undef SYS_getgid
   #undef SYS_geteuid
   #undef SYS_getegid

   #define SYS_getuid            199
   #define SYS_getgid            200
   #define SYS_geteuid           201
   #define SYS_getegid           202

   #define SYS_getuid16           24
   #define SYS_getgid16           47
   #define SYS_geteuid16          49
   #define SYS_getegid16          50

   #undef SYS_lchown
   #undef SYS_fchown
   #undef SYS_chown

   #define SYS_lchown            198
   #define SYS_fchown            207
   #define SYS_chown             212

   #define SYS_lchown16           16
   #define SYS_fchown16           95
   #define SYS_chown16           182

#else
   #error Architecture not supported
#endif
