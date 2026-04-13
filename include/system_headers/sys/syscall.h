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

#endif /* !__linux__ */
