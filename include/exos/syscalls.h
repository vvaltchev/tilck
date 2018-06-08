
#pragma once
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <exos/errno.h>
#include <exos/process.h>

#ifdef __SYSCALLS_C__

   #define CREATE_STUB_SYSCALL_IMPL(name)                  \
      sptr name()                                          \
      {                                                    \
         printk("[TID: %d] NOT IMPLEMENTED: %s()\n",       \
                get_curr_task()->tid, #name);              \
         return -ENOSYS;                                   \
      }

#else

   #define CREATE_STUB_SYSCALL_IMPL(name) sptr name();

#endif

sptr sys_exit(int code);
sptr sys_fork(void);
sptr sys_read(int fd, void *buf, size_t count);
sptr sys_write(int fd, const void *buf, size_t count);
sptr sys_open(const char *pathname, int flags, int mode);
sptr sys_close(int fd);
sptr sys_waitpid(int pid, int *wstatus, int options);

CREATE_STUB_SYSCALL_IMPL(sys_creat)
CREATE_STUB_SYSCALL_IMPL(sys_link)
CREATE_STUB_SYSCALL_IMPL(sys_unlink)

sptr sys_execve(const char *filename,
                const char *const *argv,
                const char *const *env);

sptr sys_chdir(const char *path);

CREATE_STUB_SYSCALL_IMPL(sys_time)
CREATE_STUB_SYSCALL_IMPL(sys_mknod)
CREATE_STUB_SYSCALL_IMPL(sys_chmod)
CREATE_STUB_SYSCALL_IMPL(sys_lchown16)
CREATE_STUB_SYSCALL_IMPL(sys_break)
CREATE_STUB_SYSCALL_IMPL(sys_oldstat)
CREATE_STUB_SYSCALL_IMPL(sys_lseek)

sptr sys_getpid();

CREATE_STUB_SYSCALL_IMPL(sys_mount)
CREATE_STUB_SYSCALL_IMPL(sys_oldumount)

sptr sys_setuid16(uptr uid);
sptr sys_getuid16();

CREATE_STUB_SYSCALL_IMPL(sys_stime)
CREATE_STUB_SYSCALL_IMPL(sys_ptrace)
CREATE_STUB_SYSCALL_IMPL(sys_alarm)
CREATE_STUB_SYSCALL_IMPL(sys_oldfstat)

sptr sys_pause(); // TODO: update once signals are implemented

CREATE_STUB_SYSCALL_IMPL(sys_utime)
CREATE_STUB_SYSCALL_IMPL(sys_access)
CREATE_STUB_SYSCALL_IMPL(sys_nice)
CREATE_STUB_SYSCALL_IMPL(sys_sync)
CREATE_STUB_SYSCALL_IMPL(sys_kill)
CREATE_STUB_SYSCALL_IMPL(sys_rename)
CREATE_STUB_SYSCALL_IMPL(sys_mkdir)
CREATE_STUB_SYSCALL_IMPL(sys_rmdir)
CREATE_STUB_SYSCALL_IMPL(sys_dup)
CREATE_STUB_SYSCALL_IMPL(sys_pipe)
CREATE_STUB_SYSCALL_IMPL(sys_times)
CREATE_STUB_SYSCALL_IMPL(sys_brk)
CREATE_STUB_SYSCALL_IMPL(sys_setgid16)
CREATE_STUB_SYSCALL_IMPL(sys_getgid16)
CREATE_STUB_SYSCALL_IMPL(sys_signal)
CREATE_STUB_SYSCALL_IMPL(sys_geteuid16)
CREATE_STUB_SYSCALL_IMPL(sys_getegid16)
CREATE_STUB_SYSCALL_IMPL(sys_acct)
CREATE_STUB_SYSCALL_IMPL(sys_umount)

sptr sys_ioctl(int fd, uptr request, void *argp);

CREATE_STUB_SYSCALL_IMPL(sys_fcntl)
CREATE_STUB_SYSCALL_IMPL(sys_setpgid)
CREATE_STUB_SYSCALL_IMPL(sys_olduname)
CREATE_STUB_SYSCALL_IMPL(sys_umask)
CREATE_STUB_SYSCALL_IMPL(sys_chroot)
CREATE_STUB_SYSCALL_IMPL(sys_ustat)
CREATE_STUB_SYSCALL_IMPL(sys_dup2)
CREATE_STUB_SYSCALL_IMPL(sys_getppid)
CREATE_STUB_SYSCALL_IMPL(sys_getpgrp)
CREATE_STUB_SYSCALL_IMPL(sys_setsid)
CREATE_STUB_SYSCALL_IMPL(sys_sigaction)
CREATE_STUB_SYSCALL_IMPL(sys_sgetmask)
CREATE_STUB_SYSCALL_IMPL(sys_ssetmask)


sptr sys_wait4(int pid, int *wstatus, int options, void *user_rusage);
sptr sys_writev(int fd, const void *iov, int iovcnt);
sptr sys_nanosleep();   // not fully implemented
sptr sys_rt_sigprocmask();
sptr sys_getcwd(char *buf, size_t buf_size);
sptr sys_gettid();
sptr sys_set_thread_area(void *u_info);
NORETURN sptr sys_exit_group(int status);

// TODO: complete the implementation when thread creation is implemented.
sptr sys_set_tid_address(int *tidptr);
