List of supported Linux syscalls
---------------------------------

`Tilck` supports both the `int 0x80` syscall interface and the `sysenter` one.
Here below there is an up-to-date table containing the status of each supported
syscall at the moment. Because the list of the not-yet-supported syscalls is
much longer than the list of supported ones, the table below mentions only the
syscalls having some degree of support. The missing syscalls have to be
considered as *not implemented yet*.


 Syscall             | Support level
---------------------|-------------------------
 sys_exit            | full
 sys_fork            | full
 sys_read            | full
 sys_write           | full
 sys_open            | partial [1]
 sys_close           | full
 sys_waitpid         | compliant [2]
 sys_execve          | full
 sys_chdir           | full
 sys_getpid          | full
 sys_setuid16        | compliant [3]
 sys_getuid16        | compliant [3]
 sys_pause           | stub
 sys_access          | partial
 sys_brk             | full
 sys_setgid16        | compliant [3]
 sys_getgid16        | compliant [3]
 sys_seteuid16       | compliant [3]
 sys_setegid16       | compliant [3]
 sys_ioctl           | partial
 sys_getppid         | full
 sys_gettimeofday    | full
 sys_munmap          | full
 sys_wait4           | partial [7]
 sys_newuname        | full
 sys_llseek          | full
 sys_readv           | full
 sys_writev          | full
 sys_nanosleep       | full
 sys_prctl           | stub
 sys_getcwd          | full
 sys_mmap_pgoff      | partial
 sys_stat64          | full
 sys_lstat64         | partial
 sys_getuid          | compliant [3]
 sys_getgid          | compliant [3]
 sys_geteuid         | compliant [3]
 sys_getegid         | compliant [3]
 sys_setuid          | compliant [3]
 sys_setgid          | compliant [3]
 sys_getdents64      | full
 sys_fcntl64         | stub
 sys_gettid          | minimal [4]
 sys_set_thread_area | full
 sys_exit_group      | minimal [5]
 sys_set_tid_address | stub
 sys_clock_gettime   | partial
 sys_tkill           | partial [6]
 sys_tgkill          | partial [6]
 sys_kill            | partial [6]
 sys_setsid          | minimal [8]
 sys_times           | minimal [9]

Definitions:

 Support level | Meaning
---------------|---------------------------
 full          | Syscall fully supported
 stub          | The syscall does not return -ENOSYS, but it has actually a stub
 ...           | implementation, at the moment.
 partial       | Syscall partially supported, work-in-progress
 minimal       | Like partial, just even less features are supported
 compliant     | Syscall supported in a way compliant with a full
 ...           | implementation, but actually it has several limitations due to
 ...           | the different design of Tilck. Example: see the note [3].

Notes:

1. open() work both for reading and writing files, but 'flags' and 'mode' are
   ignored at the moment. Also, it is possible only to write special files in
   /dev at the moment because the filesystem used by Tilck is only read-only.

2. The cases pid < -1, pid == -1 and pid == 0 are treated in the same way
   because Tilck does not support multiple users/groups. See note [3].

3. Tilck does not support *by design* multiple users nor any form of
   authentication. Therefore, the following statement is always true:
   UID == GID == EUID == EGID == 0.
   The syscall setuid() is compliant because it succeeds when uid is 0.

4. Because the lack of thread support, `gettid()` is the same as `getpid()`

5. Because the lack of thread support, exit_group() behaves as exit()

6. Because the lack of thread support and process groups, all of those *kill*
   syscalls behave substantially in the same way. In the case of `tgkill()` the
   the condition pid == tid is checked.

7. Currently `wait4()` behaves like `waitpid()` and the `rusage` buffer is just
   zero-ed.

8. The only thing that `setsid()` does now is resetting the controlling tty.

9. At the moment `times()` just updates `tms_utime` and `tms_stime`.
