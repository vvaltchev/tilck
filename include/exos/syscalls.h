
#pragma once
#include <common/basic_defs.h>
#include <common/string_util.h>

sptr sys_exit(int code);
sptr sys_fork(void);
sptr sys_read(int fd, void *buf, size_t count);
sptr sys_write(int fd, const void *buf, size_t count);
sptr sys_open(const char *pathname, int flags, int mode);
sptr sys_close(int fd);
sptr sys_waitpid(int pid, int *wstatus, int options);

sptr sys_creat();   // not implemented
sptr sys_link();    // not implemented
sptr sys_unlink();  // not implemented

sptr sys_execve(const char *filename,
                const char *const *argv,
                const char *const *env);

sptr sys_chdir(const char *path);


sptr sys_time();        // not implemented
sptr sys_mknod();       // not implemented
sptr sys_chmod();       // not implemented
sptr sys_lchown();      // not implemented
sptr sys_break();       // not implemented
sptr sys_oldstat();     // not implemented
sptr sys_lseek();       // not implemented

sptr sys_getpid();

sptr sys_mount();       // not implemented
sptr sys_oldumount();   // not implemented

sptr sys_setuid16(uptr uid);
sptr sys_getuid16();

sptr sys_stime();       // not implemented
sptr sys_ptrace();      // not implemented
sptr sys_alarm();       // not implemented
sptr sys_oldfstat();    // not implemented

sptr sys_pause();       // not implemented

sptr sys_utime();       // not implemented

sptr sys_ioctl(int fd, uptr request, void *argp);
sptr sys_writev(int fd, const void *iov, int iovcnt);

sptr sys_nanosleep();   // not implemented

sptr sys_getcwd(char *buf, size_t buf_size);
sptr sys_set_thread_area(void *u_info);

// TODO: complete the implementation when thread creation is implemented.
sptr sys_set_tid_address(int *tidptr);

