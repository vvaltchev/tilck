
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

CREATE_STUB_SYSCALL_IMPL(sys_restart_syscall)

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
CREATE_STUB_SYSCALL_IMPL(sys_setreuid16)
CREATE_STUB_SYSCALL_IMPL(sys_setregid16)
CREATE_STUB_SYSCALL_IMPL(sys_sigsuspend)
CREATE_STUB_SYSCALL_IMPL(sys_sigpending)
CREATE_STUB_SYSCALL_IMPL(sys_sethostname)
CREATE_STUB_SYSCALL_IMPL(sys_setrlimit)
CREATE_STUB_SYSCALL_IMPL(sys_old_getrlimit)
CREATE_STUB_SYSCALL_IMPL(sys_getrusage)
CREATE_STUB_SYSCALL_IMPL(sys_gettimeofday)
CREATE_STUB_SYSCALL_IMPL(sys_settimeofday)
CREATE_STUB_SYSCALL_IMPL(sys_getgroups16)
CREATE_STUB_SYSCALL_IMPL(sys_setgroups16)
CREATE_STUB_SYSCALL_IMPL(sys_old_select)
CREATE_STUB_SYSCALL_IMPL(sys_symlink)
CREATE_STUB_SYSCALL_IMPL(sys_lstat)
CREATE_STUB_SYSCALL_IMPL(sys_readlink)
CREATE_STUB_SYSCALL_IMPL(sys_uselib)
CREATE_STUB_SYSCALL_IMPL(sys_swapon)
CREATE_STUB_SYSCALL_IMPL(sys_reboot)
CREATE_STUB_SYSCALL_IMPL(sys_old_readdir)
CREATE_STUB_SYSCALL_IMPL(sys_old_mmap)
CREATE_STUB_SYSCALL_IMPL(sys_munmap)
CREATE_STUB_SYSCALL_IMPL(sys_truncate)
CREATE_STUB_SYSCALL_IMPL(sys_ftruncate)
CREATE_STUB_SYSCALL_IMPL(sys_fchmod)
CREATE_STUB_SYSCALL_IMPL(sys_fchown16)
CREATE_STUB_SYSCALL_IMPL(sys_getpriority)
CREATE_STUB_SYSCALL_IMPL(sys_setpriority)
CREATE_STUB_SYSCALL_IMPL(sys_statfs)
CREATE_STUB_SYSCALL_IMPL(sys_fstatfs)
CREATE_STUB_SYSCALL_IMPL(sys_ioperm)
CREATE_STUB_SYSCALL_IMPL(sys_socketcall)
CREATE_STUB_SYSCALL_IMPL(sys_syslog)
CREATE_STUB_SYSCALL_IMPL(sys_setitimer)
CREATE_STUB_SYSCALL_IMPL(sys_getitimer)
CREATE_STUB_SYSCALL_IMPL(sys_newstat)
CREATE_STUB_SYSCALL_IMPL(sys_newlstat)
CREATE_STUB_SYSCALL_IMPL(sys_newfstat)
CREATE_STUB_SYSCALL_IMPL(sys_uname)
CREATE_STUB_SYSCALL_IMPL(sys_iopl)
CREATE_STUB_SYSCALL_IMPL(sys_vhangup)
CREATE_STUB_SYSCALL_IMPL(sys_vm86old)

sptr sys_wait4(int pid, int *wstatus, int options, void *user_rusage);

CREATE_STUB_SYSCALL_IMPL(sys_swapoff)
CREATE_STUB_SYSCALL_IMPL(sys_sysinfo)
CREATE_STUB_SYSCALL_IMPL(sys_ipc)
CREATE_STUB_SYSCALL_IMPL(sys_fsync)
CREATE_STUB_SYSCALL_IMPL(sys_sigreturn)
CREATE_STUB_SYSCALL_IMPL(sys_clone)
CREATE_STUB_SYSCALL_IMPL(sys_setsetdomainname)
CREATE_STUB_SYSCALL_IMPL(sys_setnewuname)
CREATE_STUB_SYSCALL_IMPL(sys_modify_ldt)
CREATE_STUB_SYSCALL_IMPL(sys_adjtimex)
CREATE_STUB_SYSCALL_IMPL(sys_mprotect)
CREATE_STUB_SYSCALL_IMPL(sys_sigprocmask)
CREATE_STUB_SYSCALL_IMPL(sys_init_module)
CREATE_STUB_SYSCALL_IMPL(sys_delete_module)
CREATE_STUB_SYSCALL_IMPL(sys_quotactl)
CREATE_STUB_SYSCALL_IMPL(sys_getpgid)
CREATE_STUB_SYSCALL_IMPL(sys_fchdir)
CREATE_STUB_SYSCALL_IMPL(sys_bdflush)
CREATE_STUB_SYSCALL_IMPL(sys_sysfs)
CREATE_STUB_SYSCALL_IMPL(sys_personality)
CREATE_STUB_SYSCALL_IMPL(sys_setfsuid16)
CREATE_STUB_SYSCALL_IMPL(sys_setfsgid16)
CREATE_STUB_SYSCALL_IMPL(sys_llseek)
CREATE_STUB_SYSCALL_IMPL(sys_getdents)
CREATE_STUB_SYSCALL_IMPL(sys_select)
CREATE_STUB_SYSCALL_IMPL(sys_flock)
CREATE_STUB_SYSCALL_IMPL(sys_msync)
CREATE_STUB_SYSCALL_IMPL(sys_readv)

sptr sys_writev(int fd, const void *iov, int iovcnt);

CREATE_STUB_SYSCALL_IMPL(sys_getsid)
CREATE_STUB_SYSCALL_IMPL(sys_fdatasync)
CREATE_STUB_SYSCALL_IMPL(sys_sysctl)
CREATE_STUB_SYSCALL_IMPL(sys_mlock)
CREATE_STUB_SYSCALL_IMPL(sys_munlock)
CREATE_STUB_SYSCALL_IMPL(sys_mlockall)
CREATE_STUB_SYSCALL_IMPL(sys_munlockall)
CREATE_STUB_SYSCALL_IMPL(sys_sched_setparam)
CREATE_STUB_SYSCALL_IMPL(sys_sched_getparam)
CREATE_STUB_SYSCALL_IMPL(sys_sched_setscheduler)
CREATE_STUB_SYSCALL_IMPL(sys_sched_getscheduler)
CREATE_STUB_SYSCALL_IMPL(sys_sched_yield)
CREATE_STUB_SYSCALL_IMPL(sys_sched_get_priority_max)
CREATE_STUB_SYSCALL_IMPL(sys_sched_set_priority_min)
CREATE_STUB_SYSCALL_IMPL(sys_sched_rr_get_interval)

sptr sys_nanosleep();   // not fully implemented

CREATE_STUB_SYSCALL_IMPL(sys_mremap)
CREATE_STUB_SYSCALL_IMPL(sys_setresuid16)
CREATE_STUB_SYSCALL_IMPL(sys_getresuid16)
CREATE_STUB_SYSCALL_IMPL(sys_vm86)
CREATE_STUB_SYSCALL_IMPL(sys_poll)
CREATE_STUB_SYSCALL_IMPL(sys_nfsservctl)
CREATE_STUB_SYSCALL_IMPL(sys_setresgid16)
CREATE_STUB_SYSCALL_IMPL(sys_getresgid16)
CREATE_STUB_SYSCALL_IMPL(sys_prctl)
CREATE_STUB_SYSCALL_IMPL(sys_rt_sigreturn)
CREATE_STUB_SYSCALL_IMPL(sys_rt_sigaction)

sptr sys_rt_sigprocmask();

CREATE_STUB_SYSCALL_IMPL(sys_rt_sigpending)
CREATE_STUB_SYSCALL_IMPL(sys_rt_sigtimedwait)
CREATE_STUB_SYSCALL_IMPL(sys_rt_sigqueueinfo)
CREATE_STUB_SYSCALL_IMPL(sys_rt_sigsuspend)
CREATE_STUB_SYSCALL_IMPL(sys_pread64)
CREATE_STUB_SYSCALL_IMPL(sys_pwrite64)
CREATE_STUB_SYSCALL_IMPL(sys_chown16)

sptr sys_getcwd(char *buf, size_t buf_size);

CREATE_STUB_SYSCALL_IMPL(sys_capget)
CREATE_STUB_SYSCALL_IMPL(sys_capset)
CREATE_STUB_SYSCALL_IMPL(sys_sigaltstack)
CREATE_STUB_SYSCALL_IMPL(sys_sendfile)
CREATE_STUB_SYSCALL_IMPL(sys_vfork)
CREATE_STUB_SYSCALL_IMPL(sys_getrlimit)
CREATE_STUB_SYSCALL_IMPL(sys_mmap_pgoff)
CREATE_STUB_SYSCALL_IMPL(sys_truncate64)
CREATE_STUB_SYSCALL_IMPL(sys_ftruncate64)
CREATE_STUB_SYSCALL_IMPL(sys_stat64)
CREATE_STUB_SYSCALL_IMPL(sys_lstat64)
CREATE_STUB_SYSCALL_IMPL(sys_fstat64)
CREATE_STUB_SYSCALL_IMPL(sys_lchown)
CREATE_STUB_SYSCALL_IMPL(sys_getuid)
CREATE_STUB_SYSCALL_IMPL(sys_getgid)
CREATE_STUB_SYSCALL_IMPL(sys_geteuid)
CREATE_STUB_SYSCALL_IMPL(sys_getegid)
CREATE_STUB_SYSCALL_IMPL(sys_setreuid)
CREATE_STUB_SYSCALL_IMPL(sys_setregid)
CREATE_STUB_SYSCALL_IMPL(sys_getgroups)
CREATE_STUB_SYSCALL_IMPL(sys_setgroups)
CREATE_STUB_SYSCALL_IMPL(sys_fchown)
CREATE_STUB_SYSCALL_IMPL(sys_setresuid)
CREATE_STUB_SYSCALL_IMPL(sys_getresuid)
CREATE_STUB_SYSCALL_IMPL(sys_setresgid)
CREATE_STUB_SYSCALL_IMPL(sys_getresgid)
CREATE_STUB_SYSCALL_IMPL(sys_chown)
CREATE_STUB_SYSCALL_IMPL(sys_setuid)
CREATE_STUB_SYSCALL_IMPL(sys_setgid)
CREATE_STUB_SYSCALL_IMPL(sys_setfsuid)
CREATE_STUB_SYSCALL_IMPL(sys_setfsgid)
CREATE_STUB_SYSCALL_IMPL(sys_pivot_root)
CREATE_STUB_SYSCALL_IMPL(sys_mincore)
CREATE_STUB_SYSCALL_IMPL(sys_madvise)
CREATE_STUB_SYSCALL_IMPL(sys_getdents64)
CREATE_STUB_SYSCALL_IMPL(sys_fcntl64)

sptr sys_gettid();
sptr sys_set_thread_area(void *u_info);
NORETURN sptr sys_exit_group(int status);

// TODO: complete the implementation when thread creation is implemented.
sptr sys_set_tid_address(int *tidptr);
