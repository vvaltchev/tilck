
#pragma once
#include <common/basic_defs.h>
#include <common/string_util.h>

#define DEFINE_STUB_SYSCALL(name) sptr name()

sptr sys_exit(int code);
sptr sys_fork(void);
sptr sys_read(int fd, void *buf, size_t count);
sptr sys_write(int fd, const void *buf, size_t count);
sptr sys_open(const char *pathname, int flags, int mode);
sptr sys_close(int fd);
sptr sys_waitpid(int pid, int *wstatus, int options);

DEFINE_STUB_SYSCALL(sys_creat);
DEFINE_STUB_SYSCALL(sys_link);
DEFINE_STUB_SYSCALL(sys_unlink);

sptr sys_execve(const char *filename,
                const char *const *argv,
                const char *const *env);

sptr sys_chdir(const char *path);

DEFINE_STUB_SYSCALL(sys_time);
DEFINE_STUB_SYSCALL(sys_mknod);
DEFINE_STUB_SYSCALL(sys_chmod);
DEFINE_STUB_SYSCALL(sys_lchown);
DEFINE_STUB_SYSCALL(sys_break);
DEFINE_STUB_SYSCALL(sys_oldstat);
DEFINE_STUB_SYSCALL(sys_lseek);

sptr sys_getpid();

DEFINE_STUB_SYSCALL(sys_mount);
DEFINE_STUB_SYSCALL(sys_oldumount);

sptr sys_setuid16(uptr uid);
sptr sys_getuid16();

DEFINE_STUB_SYSCALL(sys_stime);
DEFINE_STUB_SYSCALL(sys_ptrace);
DEFINE_STUB_SYSCALL(sys_alarm);
DEFINE_STUB_SYSCALL(sys_oldfstat);

sptr sys_pause(); // TODO: update once signals are implemented

DEFINE_STUB_SYSCALL(sys_utime);

sptr sys_ioctl(int fd, uptr request, void *argp);

DEFINE_STUB_SYSCALL(sys_wait4);

sptr sys_writev(int fd, const void *iov, int iovcnt);

sptr sys_nanosleep();   // not fully implemented
DEFINE_STUB_SYSCALL(sys_rt_sigprocmask);


sptr sys_getcwd(char *buf, size_t buf_size);
sptr sys_set_thread_area(void *u_info);

DEFINE_STUB_SYSCALL(sys_exit_group);

// TODO: complete the implementation when thread creation is implemented.
sptr sys_set_tid_address(int *tidptr);

