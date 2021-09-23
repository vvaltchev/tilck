List of supported Linux syscalls
---------------------------------

`Tilck` supports both the `int 0x80` syscall interface and the `sysenter` one.
Here below there is an up-to-date table containing the status of each supported
syscall at the moment. Because the list of the not-yet-supported syscalls is
much longer than the list of supported ones, the table below mentions only the
syscalls having some degree of support. The missing syscalls have to be
considered as *not implemented yet*.


 Syscall                    | Support level
----------------------------|-------------------------
 sys_exit                   | full
 sys_fork                   | full
 sys_read                   | full
 sys_write                  | full
 sys_open                   | partial++ [1]
 sys_close                  | full
 sys_waitpid                | full
 sys_execve                 | full
 sys_chdir                  | full
 sys_getpid                 | full
 sys_setuid16               | limited [3]
 sys_getuid16               | limited [3]
 sys_pause                  | minimal [2]
 sys_access                 | partial
 sys_brk                    | full
 sys_setgid16               | limited [3]
 sys_getgid16               | limited [3]
 sys_seteuid16              | limited [3]
 sys_setegid16              | limited [3]
 sys_geteuid16              | limited [3]
 sys_getegid16              | limited [3]
 sys_ioctl                  | partial
 sys_getppid                | full
 sys_gettimeofday           | full
 sys_munmap                 | full
 sys_wait4                  | partial [7]
 sys_newuname               | full
 sys_llseek                 | full
 sys_readv                  | full
 sys_writev                 | full
 sys_nanosleep_time32       | full
 sys_prctl                  | stub
 sys_getcwd                 | full
 sys_mmap_pgoff             | full
 sys_stat64                 | full
 sys_fstat64                | full
 sys_lstat64                | partial
 sys_getuid                 | limited [3]
 sys_getgid                 | limited [3]
 sys_geteuid                | limited [3]
 sys_getegid                | limited [3]
 sys_setuid                 | limited [3]
 sys_setgid                 | limited [3]
 sys_getdents64             | full
 sys_fcntl64                | partial
 sys_gettid                 | minimal [4]
 sys_set_thread_area        | full
 sys_exit_group             | minimal [5]
 sys_set_tid_address        | stub
 sys_tkill                  | partial++ [6]
 sys_tgkill                 | partial++ [6]
 sys_kill                   | full
 sys_setsid                 | full
 sys_times                  | minimal [9]
 sys_clock_gettime          | compliant [10]
 sys_clock_gettime32        | compliant [10]
 sys_clock_getres           | compliant [10]
 sys_clock_getres_time32    | compliant [10]
 sys_select                 | full
 sys_poll                   | full
 sys_readlink               | full
 sys_creat                  | full
 sys_unlink                 | full
 sys_symlink                | full
 sys_vfork                  | compliant [11]
 sys_umask                  | full
 sys_ia32_truncate64        | full
 sys_ia32_ftruncate64       | full
 sys_sync                   | full
 sys_syncfs                 | full
 sys_chown                  | limited [3]
 sys_fchown                 | limited [3]
 sys_lchown                 | limited [3]
 sys_chmod                  | full
 sys_fchmod                 | full
 sys_rename                 | full
 sys_link                   | full
 sys_pipe                   | full
 sys_pipe2                  | partial++ [13]
 sys_sched_yield            | full
 sys_getsid                 | full
 sys_setpgid                | full
 sys_getpgid                | full
 sys_getpgrp                | full
 sys_utime32                | full
 sys_utimes                 | full
 sys_fsync                  | compliant
 sys_fdatasync              | compliant
 sys_dup                    | full
 sys_dup2                   | full
 sys_reboot                 | full
 sys_rt_sigprocmask         | full
 sys_rt_sigpending          | full
 sys_rt_sigreturn           | partial [14]
 sys_rt_sigaction           | partial [14]
 sys_rt_sigsuspend          | partial [14]


Definitions:

 Support level | Meaning
---------------|---------------------------
 full          | Syscall 100% supported.
 partial       | Syscall partially supported, work-in-progress
 partial++     | All of the really important flags and modes are supported
 ...           | Most of the advanced stuff very likely is still not supported.
 stub          | The syscall does not return -ENOSYS, but it has actually a stub
 ...           | implementation, at the moment.
 minimal       | Like partial, just even less features are supported
 compliant     | Syscall supported in a way compliant with a full
 ...           | implementation, but actually it has several limitations due to
 ...           | the different design of Tilck. Example: see the note [3].
 limited       | The syscall supports by design only a subset of the cases
 ...           | supported by the Linux implementation.

Notes:

1. The syscall open() now supports read/write access, file creation, mode
   setting and flags like O_APPEND, O_CLOEXEC, O_EXCL, O_TRUNC. All the
   "advanced" flags like O_ASYNC are not supported yet.

2. Because custom signal handlers are not supported, pause() puts the process
   to sleep until a signal actually kills it.

3. Tilck does not support *by design* multiple users nor any form of
   authentication. Therefore, the following statement is always true:
   UID == GID == EUID == EGID == 0. All the calls like setuid(), seteuid(),
   setgid(), setegid(), chown() etc. succeed only when UID/GID == 0.

4. Because the lack of thread support, `gettid()` is the same as `getpid()`

5. Because the lack of thread support, exit_group() behaves as exit()

6. Because the lack of thread support, `tgkill()` works only when
   `tgid` == `tid`.

7. Currently `wait4()` behaves like `waitpid()` and the `rusage` buffer is just
   zero-ed.

8. [Limitation removed]

9. At the moment `times()` just updates `tms_utime` and `tms_stime`.

10. Only the clocks CLOCK_REALTIME and CLOCK_MONOTONIC are supported.

11. Behaves exactly as `fork()`.

12. [Limitation removed]

13. The O_DIRECT mode is not supported.

14. Tilck has a partial support for POSIX signals. SIG_IGN and SIG_DFL are
    fully supported. Custom signal handlers are supported as well, but signal
    handlers (even of different type) cannot interrupt each other. In other
    words, nested custom signal handlers are not supported, yet. Also, even if
    the modern sys_rt_sigaction() interface is implemented, none of its flags
    like SA_NODEFER, SA_ONSTACK, SA_RESETHAND, SA_RESTART, SA_SIGINFO are
    supported, at the moment. The per-signal sa_mask is also fixed: while a
    custom handler is running, all the other signals having a custom handler
    are masked (terminating signals instead, can always we delieved).
    In addition, the order of delivery of all signals is unspecified and there
    is no distinction between regular signals and real-time ones. Also, at the
    moment, no syscall can be restarted if a signal interrupted it. The syscall
    sys_rt_sigsuspend() works as expected if we're not calling it from a signal
    handler otherwise, it returns -EPERM (not allowed according to POSIX).

    NOTE: while the just-described limited support for POSIX reliable signals
    might seem too limited, it's worth noting that it already opened a
    considerable amount of uses, like graceful process termination with SIGTERM.
