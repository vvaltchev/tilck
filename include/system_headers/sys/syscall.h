/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shim for <sys/syscall.h>.
 * On non-Linux hosts (FreeBSD, macOS) the system header provides the
 * native syscall numbers, not the Linux ones that Tilck expects.
 * Define any missing Linux x86_64 SYS_* constants after including
 * the real header so that the kernel source compiles in the unit-test
 * environment.
 */

#pragma once

#include_next <sys/syscall.h>

#ifndef __linux__

#ifndef SYS_stat
#define SYS_stat               4
#endif
#ifndef SYS_lstat
#define SYS_lstat              6
#endif
#ifndef SYS_brk
#define SYS_brk                12
#endif
#ifndef SYS_rt_sigaction
#define SYS_rt_sigaction       13
#endif
#ifndef SYS_rt_sigprocmask
#define SYS_rt_sigprocmask     14
#endif
#ifndef SYS_pipe
#define SYS_pipe               22
#endif
#ifndef SYS_pause
#define SYS_pause              34
#endif
#ifndef SYS_uname
#define SYS_uname              63
#endif
#ifndef SYS_getcwd
#define SYS_getcwd             79
#endif
#ifndef SYS_creat
#define SYS_creat              85
#endif
#ifndef SYS_prctl
#define SYS_prctl              157
#endif
#ifndef SYS_gettid
#define SYS_gettid             186
#endif
#ifndef SYS_tkill
#define SYS_tkill              200
#endif
#ifndef SYS_set_thread_area
#define SYS_set_thread_area    205
#endif
#ifndef SYS_set_tid_address
#define SYS_set_tid_address    218
#endif
#ifndef SYS_exit_group
#define SYS_exit_group         231
#endif

/*
 * Linux-universal syscalls referenced unconditionally by the kernel
 * source (e.g. modules/tracing/tracing_metadata.c). All Tilck target
 * arches (i386, riscv64, x86_64) define them; darwin does not. The
 * values below are the Linux x86_64 numbers — they only need to make
 * the host build compile, the unit tests never invoke real syscalls
 * with them.
 */
#ifndef SYS_nanosleep
#define SYS_nanosleep          35
#endif
#ifndef SYS_clone
#define SYS_clone              56
#endif
#ifndef SYS_times
#define SYS_times              100
#endif
#ifndef SYS_rt_sigpending
#define SYS_rt_sigpending      127
#endif
#ifndef SYS_rt_sigsuspend
#define SYS_rt_sigsuspend      130
#endif
#ifndef SYS_pread64
#define SYS_pread64            17
#endif
#ifndef SYS_pwrite64
#define SYS_pwrite64           18
#endif
#ifndef SYS_getdents64
#define SYS_getdents64         217
#endif
#ifndef SYS_tgkill
#define SYS_tgkill             234
#endif
#ifndef SYS_umount2
#define SYS_umount2            166
#endif
#ifndef SYS_utimensat
#define SYS_utimensat          280
#endif
#ifndef SYS_pselect6
#define SYS_pselect6           270
#endif
#ifndef SYS_ppoll
#define SYS_ppoll              271
#endif
#ifndef SYS_dup3
#define SYS_dup3               292
#endif
#ifndef SYS_pipe2
#define SYS_pipe2              293
#endif
#ifndef SYS_renameat2
#define SYS_renameat2          316
#endif
#ifndef SYS_sched_yield
#define SYS_sched_yield        24
#endif
#ifndef SYS_syncfs
#define SYS_syncfs             306
#endif

#endif /* !__linux__ */
