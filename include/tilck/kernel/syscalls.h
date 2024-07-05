/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/syscalls.h>

#include <tilck/kernel/errno.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/sys_types.h>

#include <tilck/mods/tracing.h>

#ifdef __SYSCALLS_C__

   #define CREATE_STUB_SYSCALL_IMPL(name)                          \
      int name()                                                   \
      {                                                            \
         if (KRN_NO_SYS_WARN) {                                    \
            printk("[TID: %d] NOT IMPLEMENTED: %s()\n",            \
                   get_curr_tid(), #name);                         \
         }                                                         \
         trace_printk(5, "Syscall NOT IMPLEMENTED: %s()", #name);  \
         return -ENOSYS;                                           \
      }

#else

   #define CREATE_STUB_SYSCALL_IMPL(name) long name();

#endif

CREATE_STUB_SYSCALL_IMPL(sys_restart_syscall);

int sys_exit(int code);
int sys_fork(void *u_regs);
int sys_read(int fd, void *buf, size_t count);
int sys_write(int fd, const void *buf, size_t count);
int sys_open(const char *u_path, int flags, mode_t mode);
int sys_close(int fd);
int sys_waitpid(int pid, int *u_wstatus, int options);
int sys_creat(const char *u_path, mode_t mode);
int sys_link(const char *u_oldpath, const char *u_newpath);
int sys_unlink(const char *u_path);
int sys_execve(const char *u_path,
               const char *const *argv,
               const char *const *env);
int sys_chdir(const char *path);

CREATE_STUB_SYSCALL_IMPL(sys_time)
CREATE_STUB_SYSCALL_IMPL(sys_mknod)

int sys_chmod(const char *u_path, mode_t mode);

CREATE_STUB_SYSCALL_IMPL(sys_lchown16)
/* sys_break does NOT exist on Linux, see break(2) */
CREATE_STUB_SYSCALL_IMPL(sys_oldstat)
CREATE_STUB_SYSCALL_IMPL(sys_lseek)

int sys_getpid(void);
int sys_mount(const char *u_source,
              const char *u_target,
              const char *u_fstype,
              unsigned long mountflags,
              const void *u_data);

CREATE_STUB_SYSCALL_IMPL(sys_oldumount)

int sys_setuid16(ulong uid);
int sys_getuid16(void);

CREATE_STUB_SYSCALL_IMPL(sys_stime32)
CREATE_STUB_SYSCALL_IMPL(sys_ptrace)
CREATE_STUB_SYSCALL_IMPL(sys_alarm)
CREATE_STUB_SYSCALL_IMPL(sys_oldfstat)

int sys_pause(void);
int sys_utime32(const char *u_path, const struct k_utimbuf *u_times);
int sys_access(const char *u_path, mode_t mode);

CREATE_STUB_SYSCALL_IMPL(sys_nice)

int sys_sync(void);
int sys_kill(int pid, int sig);
int sys_rename(const char *u_oldpath, const char *u_newpath);
int sys_mkdir(const char *u_path, mode_t mode);
int sys_rmdir(const char *u_path);
int sys_dup(int oldfd);

int sys_pipe(int u_pipefd[2]);

ulong sys_times(struct tms *u_buf);
void *sys_brk(void *vaddr);
int sys_setgid16(ulong gid);
int sys_getgid16(void);

__sighandler_t sys_signal(int signum, __sighandler_t handler); // deprecated

int sys_geteuid16(void);
int sys_getegid16(void);

CREATE_STUB_SYSCALL_IMPL(sys_acct)

int sys_umount(const char *target, int flags);
int sys_ioctl(int fd, ulong request, void *argp);

CREATE_STUB_SYSCALL_IMPL(sys_fcntl)

int sys_setpgid(int pid, int pgid);

CREATE_STUB_SYSCALL_IMPL(sys_olduname)

mode_t sys_umask(mode_t mask);

CREATE_STUB_SYSCALL_IMPL(sys_chroot)
CREATE_STUB_SYSCALL_IMPL(sys_ustat)

int sys_dup2(int oldfd, int newfd);
int sys_getppid(void);
int sys_getpgrp(void);
int sys_setsid(void);

int sys_sigaction(ulong a1, ulong a2, ulong a3); // deprecated interface

CREATE_STUB_SYSCALL_IMPL(sys_sgetmask)
CREATE_STUB_SYSCALL_IMPL(sys_ssetmask)
CREATE_STUB_SYSCALL_IMPL(sys_setreuid16)
CREATE_STUB_SYSCALL_IMPL(sys_setregid16)
CREATE_STUB_SYSCALL_IMPL(sys_sigsuspend)
CREATE_STUB_SYSCALL_IMPL(sys_sigpending)
CREATE_STUB_SYSCALL_IMPL(sys_sethostname)
CREATE_STUB_SYSCALL_IMPL(sys_setrlimit)
CREATE_STUB_SYSCALL_IMPL(sys_old_getrlimit)

int sys_getrusage(int who, struct k_rusage *user_buf);
int sys_gettimeofday(struct k_timeval *tv, struct timezone *tz);

CREATE_STUB_SYSCALL_IMPL(sys_settimeofday)
CREATE_STUB_SYSCALL_IMPL(sys_getgroups16)
CREATE_STUB_SYSCALL_IMPL(sys_setgroups16)
CREATE_STUB_SYSCALL_IMPL(sys_old_select)

int sys_symlink(const char *u_target, const char *u_linkpath);

CREATE_STUB_SYSCALL_IMPL(sys_lstat)

int sys_readlink(const char *u_pathname, char *u_buf, size_t bufsize);

CREATE_STUB_SYSCALL_IMPL(sys_uselib)
CREATE_STUB_SYSCALL_IMPL(sys_swapon)

int sys_reboot(u32 magic, u32 magic2, u32 cmd, void *arg);

CREATE_STUB_SYSCALL_IMPL(sys_old_readdir)
CREATE_STUB_SYSCALL_IMPL(sys_old_mmap)

int sys_munmap(void *vaddr, size_t len);

CREATE_STUB_SYSCALL_IMPL(sys_truncate)
CREATE_STUB_SYSCALL_IMPL(sys_ftruncate)

int sys_fchmod(int fd, mode_t mode);

CREATE_STUB_SYSCALL_IMPL(sys_fchown16)
CREATE_STUB_SYSCALL_IMPL(sys_getpriority)
CREATE_STUB_SYSCALL_IMPL(sys_setpriority)
CREATE_STUB_SYSCALL_IMPL(sys_statfs)
CREATE_STUB_SYSCALL_IMPL(sys_fstatfs)
CREATE_STUB_SYSCALL_IMPL(sys_ioperm)

int sys_socketcall(int call, ulong *args);

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

int sys_wait4(int pid, int *u_wstatus, int options, void *u_rusage);

CREATE_STUB_SYSCALL_IMPL(sys_swapoff)
CREATE_STUB_SYSCALL_IMPL(sys_sysinfo)
CREATE_STUB_SYSCALL_IMPL(sys_ipc)

int sys_fsync(int fd);
CREATE_STUB_SYSCALL_IMPL(sys_sigreturn);

CREATE_STUB_SYSCALL_IMPL(sys_clone)
CREATE_STUB_SYSCALL_IMPL(sys_setdomainname)

int sys_newuname(struct utsname *buf);

CREATE_STUB_SYSCALL_IMPL(sys_modify_ldt)
CREATE_STUB_SYSCALL_IMPL(sys_adjtimex_time32)
CREATE_STUB_SYSCALL_IMPL(sys_mprotect)

int sys_sigprocmask(ulong a1, ulong a2, ulong a3); // deprecated interface

CREATE_STUB_SYSCALL_IMPL(sys_init_module)
CREATE_STUB_SYSCALL_IMPL(sys_delete_module)
CREATE_STUB_SYSCALL_IMPL(sys_quotactl)

int sys_getpgid(int pid);

CREATE_STUB_SYSCALL_IMPL(sys_fchdir)
CREATE_STUB_SYSCALL_IMPL(sys_bdflush)
CREATE_STUB_SYSCALL_IMPL(sys_sysfs)
CREATE_STUB_SYSCALL_IMPL(sys_personality)
CREATE_STUB_SYSCALL_IMPL(sys_setfsuid16)
CREATE_STUB_SYSCALL_IMPL(sys_setfsgid16)

int sys_llseek(int fd, size_t off_hi, size_t off_low, u64 *result, u32 whence);

CREATE_STUB_SYSCALL_IMPL(sys_getdents)

int sys_select(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, struct k_timeval *timeout);

CREATE_STUB_SYSCALL_IMPL(sys_flock)
CREATE_STUB_SYSCALL_IMPL(sys_msync)

int sys_readv(int fd, const struct iovec *iov, int iovcnt);
int sys_writev(int fd, const struct iovec *iov, int iovcnt);
int sys_getsid(int pid);
int sys_fdatasync(int fd);

CREATE_STUB_SYSCALL_IMPL(sys_sysctl)
CREATE_STUB_SYSCALL_IMPL(sys_mlock)
CREATE_STUB_SYSCALL_IMPL(sys_munlock)
CREATE_STUB_SYSCALL_IMPL(sys_mlockall)
CREATE_STUB_SYSCALL_IMPL(sys_munlockall)
CREATE_STUB_SYSCALL_IMPL(sys_sched_setparam)
CREATE_STUB_SYSCALL_IMPL(sys_sched_getparam)
CREATE_STUB_SYSCALL_IMPL(sys_sched_setscheduler)
CREATE_STUB_SYSCALL_IMPL(sys_sched_getscheduler)

int sys_sched_yield(void);

CREATE_STUB_SYSCALL_IMPL(sys_sched_get_priority_max)
CREATE_STUB_SYSCALL_IMPL(sys_sched_set_priority_min)
CREATE_STUB_SYSCALL_IMPL(sys_sched_rr_get_interval_time32)

int sys_nanosleep_time32(const struct k_timespec32 *req,
                         struct k_timespec32 *rem);

CREATE_STUB_SYSCALL_IMPL(sys_mremap)
CREATE_STUB_SYSCALL_IMPL(sys_setresuid16)
CREATE_STUB_SYSCALL_IMPL(sys_getresuid16)
CREATE_STUB_SYSCALL_IMPL(sys_vm86)

int sys_poll(struct pollfd *fds, nfds_t nfds, int timeout);

CREATE_STUB_SYSCALL_IMPL(sys_nfsservctl)
CREATE_STUB_SYSCALL_IMPL(sys_setresgid16)
CREATE_STUB_SYSCALL_IMPL(sys_getresgid16)

int sys_prctl(int option, ulong a2, ulong a3, ulong a4, ulong a5);
ulong sys_rt_sigreturn(void);

int
sys_rt_sigaction(int signum,
                 const struct k_sigaction *act,
                 struct k_sigaction *oldact,
                 size_t sigsetsize);

int
sys_rt_sigprocmask(int how, sigset_t *u_set,
                   sigset_t *u_oldset, size_t sigsetsize);

int
sys_rt_sigpending(sigset_t *u_set, size_t sigsetsize);

CREATE_STUB_SYSCALL_IMPL(sys_rt_sigtimedwait_time32)
CREATE_STUB_SYSCALL_IMPL(sys_rt_sigqueueinfo)

int sys_rt_sigsuspend(sigset_t *u_mask, size_t sigsetsize);
int sys_pread64(int fd, void *buf, size_t count, s64 off);
int sys_pwrite64(int fd, const void *buf, size_t count, s64 off);

CREATE_STUB_SYSCALL_IMPL(sys_chown16)

int sys_getcwd(char *buf, size_t size);

CREATE_STUB_SYSCALL_IMPL(sys_capget)
CREATE_STUB_SYSCALL_IMPL(sys_capset)
CREATE_STUB_SYSCALL_IMPL(sys_sigaltstack)
CREATE_STUB_SYSCALL_IMPL(sys_sendfile)

int sys_vfork(void *u_regs);

CREATE_STUB_SYSCALL_IMPL(sys_getrlimit)

long sys_mmap_pgoff(void *addr, size_t length, int prot,
                    int flags, int fd, size_t pgoffset);

int sys_ia32_truncate64(const char *u_path, s64 length);
int sys_ia32_ftruncate64(int fd, s64 length);
int sys_stat64(const char *u_path, struct k_stat64 *u_statbuf);
int sys_lstat64(const char *u_path, struct k_stat64 *u_statbuf);
int sys_fstat64(int fd, struct k_stat64 *u_statbuf);
int sys_lchown(const char *u_path, int owner, int group);

int sys_getuid(void);
int sys_getgid(void);
int sys_geteuid(void);
int sys_getegid(void);

CREATE_STUB_SYSCALL_IMPL(sys_setreuid)
CREATE_STUB_SYSCALL_IMPL(sys_setregid)
CREATE_STUB_SYSCALL_IMPL(sys_getgroups)
CREATE_STUB_SYSCALL_IMPL(sys_setgroups)

int sys_fchown(int fd, uid_t owner, gid_t group);

CREATE_STUB_SYSCALL_IMPL(sys_setresuid)
CREATE_STUB_SYSCALL_IMPL(sys_getresuid)
CREATE_STUB_SYSCALL_IMPL(sys_setresgid)
CREATE_STUB_SYSCALL_IMPL(sys_getresgid)

int sys_chown(const char *u_path, int owner, int group);
int sys_setuid(ulong uid);
int sys_setgid(ulong gid);

CREATE_STUB_SYSCALL_IMPL(sys_setfsuid)
CREATE_STUB_SYSCALL_IMPL(sys_setfsgid)
CREATE_STUB_SYSCALL_IMPL(sys_pivot_root)
CREATE_STUB_SYSCALL_IMPL(sys_mincore)

int sys_madvise(void *addr, size_t len, int advice);
int sys_getdents64(int fd, struct linux_dirent64 *dirp, u32 buf_size);
int sys_fcntl64(int fd, int cmd, int arg);
int sys_gettid(void);

CREATE_STUB_SYSCALL_IMPL(sys_ia32_readahead)
CREATE_STUB_SYSCALL_IMPL(sys_setxattr)
CREATE_STUB_SYSCALL_IMPL(sys_lsetxattr)
CREATE_STUB_SYSCALL_IMPL(sys_fsetxattr)
CREATE_STUB_SYSCALL_IMPL(sys_getxattr)
CREATE_STUB_SYSCALL_IMPL(sys_lgetxattr)
CREATE_STUB_SYSCALL_IMPL(sys_fgetxattr)
CREATE_STUB_SYSCALL_IMPL(sys_listxattr)
CREATE_STUB_SYSCALL_IMPL(sys_llistxattr)
CREATE_STUB_SYSCALL_IMPL(sys_flistxattr)
CREATE_STUB_SYSCALL_IMPL(sys_removexattr)
CREATE_STUB_SYSCALL_IMPL(sys_lremovexattr)
CREATE_STUB_SYSCALL_IMPL(sys_fremovexattr)

int sys_tkill(int tid, int sig);

CREATE_STUB_SYSCALL_IMPL(sys_sendfile64)
CREATE_STUB_SYSCALL_IMPL(sys_futex_time32)
CREATE_STUB_SYSCALL_IMPL(sys_sched_setaffinity)
CREATE_STUB_SYSCALL_IMPL(sys_sched_getaffinity)

int sys_set_thread_area(void *u_info);

CREATE_STUB_SYSCALL_IMPL(sys_get_thread_area)
CREATE_STUB_SYSCALL_IMPL(sys_io_setup)
CREATE_STUB_SYSCALL_IMPL(sys_io_destroy)
CREATE_STUB_SYSCALL_IMPL(sys_io_getevents_time32)
CREATE_STUB_SYSCALL_IMPL(sys_io_submit)
CREATE_STUB_SYSCALL_IMPL(sys_io_cancel)
CREATE_STUB_SYSCALL_IMPL(sys_ia32_fadvise64)

NORETURN int sys_exit_group(int status);

CREATE_STUB_SYSCALL_IMPL(sys_lookup_dcookie)
CREATE_STUB_SYSCALL_IMPL(sys_epoll_create)
CREATE_STUB_SYSCALL_IMPL(sys_epoll_ctl)
CREATE_STUB_SYSCALL_IMPL(sys_epoll_wait)
CREATE_STUB_SYSCALL_IMPL(sys_remap_file_pages)

// TODO: complete the implementation when thread creation is implemented.
int sys_set_tid_address(int *tidptr);

CREATE_STUB_SYSCALL_IMPL(sys_timer_create)
CREATE_STUB_SYSCALL_IMPL(sys_timer_settime32)
CREATE_STUB_SYSCALL_IMPL(sys_timer_gettime32)
CREATE_STUB_SYSCALL_IMPL(sys_timer_getoverrun)
CREATE_STUB_SYSCALL_IMPL(sys_timer_delete)
CREATE_STUB_SYSCALL_IMPL(sys_clock_settime32)

int sys_clock_gettime32(clockid_t clk_id, struct k_timespec32 *tp);
int sys_clock_getres_time32(clockid_t clk_id, struct k_timespec32 *res);

CREATE_STUB_SYSCALL_IMPL(sys_clock_nanosleep_time32)
CREATE_STUB_SYSCALL_IMPL(sys_statfs64)
CREATE_STUB_SYSCALL_IMPL(sys_fstatfs64)

int sys_tgkill(int pid /* linux: tgid */, int tid, int sig);
int sys_utimes(const char *u_path, const struct k_timeval u_times[2]);

CREATE_STUB_SYSCALL_IMPL(sys_ia32_fadvise64_64)
CREATE_STUB_SYSCALL_IMPL(sys_mbind)
CREATE_STUB_SYSCALL_IMPL(sys_get_mempolicy)
CREATE_STUB_SYSCALL_IMPL(sys_set_mempolicy)
CREATE_STUB_SYSCALL_IMPL(sys_mq_open)
CREATE_STUB_SYSCALL_IMPL(sys_mq_unlink)
CREATE_STUB_SYSCALL_IMPL(sys_mq_timedsend_time32)
CREATE_STUB_SYSCALL_IMPL(sys_mq_timedreceive_time32)
CREATE_STUB_SYSCALL_IMPL(sys_mq_notify)
CREATE_STUB_SYSCALL_IMPL(sys_mq_getsetattr)
CREATE_STUB_SYSCALL_IMPL(sys_kexec_load)
CREATE_STUB_SYSCALL_IMPL(sys_waitid)
CREATE_STUB_SYSCALL_IMPL(sys_add_key)
CREATE_STUB_SYSCALL_IMPL(sys_request_key)
CREATE_STUB_SYSCALL_IMPL(sys_keyctl)
CREATE_STUB_SYSCALL_IMPL(sys_ioprio_set)
CREATE_STUB_SYSCALL_IMPL(sys_ioprio_get)
CREATE_STUB_SYSCALL_IMPL(sys_inotify_init)
CREATE_STUB_SYSCALL_IMPL(sys_inotify_add_watch)
CREATE_STUB_SYSCALL_IMPL(sys_inotify_rm_watch)
CREATE_STUB_SYSCALL_IMPL(sys_migrate_pages)
CREATE_STUB_SYSCALL_IMPL(sys_openat)
CREATE_STUB_SYSCALL_IMPL(sys_mkdirat)
CREATE_STUB_SYSCALL_IMPL(sys_mknodat)
CREATE_STUB_SYSCALL_IMPL(sys_fchownat)

int sys_futimesat_time32(int dirfd, const char *u_path,
                  const struct k_timeval times[2]);

CREATE_STUB_SYSCALL_IMPL(sys_fstatat64)
CREATE_STUB_SYSCALL_IMPL(sys_unlinkat)
CREATE_STUB_SYSCALL_IMPL(sys_renameat)
CREATE_STUB_SYSCALL_IMPL(sys_linkat)
CREATE_STUB_SYSCALL_IMPL(sys_symlinkat)
CREATE_STUB_SYSCALL_IMPL(sys_readlinkat)
CREATE_STUB_SYSCALL_IMPL(sys_fchmodat)
CREATE_STUB_SYSCALL_IMPL(sys_faccessat)
CREATE_STUB_SYSCALL_IMPL(sys_pselect6)
CREATE_STUB_SYSCALL_IMPL(sys_ppoll)
CREATE_STUB_SYSCALL_IMPL(sys_unshare)
CREATE_STUB_SYSCALL_IMPL(sys_set_robust_list)
CREATE_STUB_SYSCALL_IMPL(sys_get_robust_list)
CREATE_STUB_SYSCALL_IMPL(sys_splice)
CREATE_STUB_SYSCALL_IMPL(sys_ia32_sync_file_range)
CREATE_STUB_SYSCALL_IMPL(sys_tee)
CREATE_STUB_SYSCALL_IMPL(sys_vmsplice)
CREATE_STUB_SYSCALL_IMPL(sys_move_pages)
CREATE_STUB_SYSCALL_IMPL(sys_getcpu)
CREATE_STUB_SYSCALL_IMPL(sys_epoll_pwait)

int sys_utimensat_time32(int dirfd, const char *u_path,
                         const struct k_timespec32 times[2], int flags);

CREATE_STUB_SYSCALL_IMPL(sys_signalfd)
CREATE_STUB_SYSCALL_IMPL(sys_timerfd_create)
CREATE_STUB_SYSCALL_IMPL(sys_eventfd)
CREATE_STUB_SYSCALL_IMPL(sys_fallocate)
CREATE_STUB_SYSCALL_IMPL(sys_timerfd_settime32)
CREATE_STUB_SYSCALL_IMPL(sys_timerfd_gettime32)
CREATE_STUB_SYSCALL_IMPL(sys_signalfd4)
CREATE_STUB_SYSCALL_IMPL(sys_eventfd2)
CREATE_STUB_SYSCALL_IMPL(sys_epoll_create1)
CREATE_STUB_SYSCALL_IMPL(sys_dup3)

int sys_pipe2(int u_pipefd[2], int flags);

CREATE_STUB_SYSCALL_IMPL(sys_inotify_init1)
CREATE_STUB_SYSCALL_IMPL(sys_preadv)
CREATE_STUB_SYSCALL_IMPL(sys_pwritev)
CREATE_STUB_SYSCALL_IMPL(sys_rt_tgsigqueueinfo)
CREATE_STUB_SYSCALL_IMPL(sys_perf_event_open)
CREATE_STUB_SYSCALL_IMPL(sys_recvmmsg_time32)

CREATE_STUB_SYSCALL_IMPL(sys_fanotify_init)
CREATE_STUB_SYSCALL_IMPL(sys_fanotify_mark)
CREATE_STUB_SYSCALL_IMPL(sys_prlimit64)
CREATE_STUB_SYSCALL_IMPL(sys_name_to_handle_at)
CREATE_STUB_SYSCALL_IMPL(sys_open_by_handle_at)
CREATE_STUB_SYSCALL_IMPL(sys_clock_adjtime32)

int sys_syncfs(int fd);

CREATE_STUB_SYSCALL_IMPL(sys_sendmmsg)
CREATE_STUB_SYSCALL_IMPL(sys_setns)
CREATE_STUB_SYSCALL_IMPL(sys_process_vm_readv)
CREATE_STUB_SYSCALL_IMPL(sys_process_vm_writev)
CREATE_STUB_SYSCALL_IMPL(sys_kcmp)
CREATE_STUB_SYSCALL_IMPL(sys_finit_module)
CREATE_STUB_SYSCALL_IMPL(sys_sched_setattr)
CREATE_STUB_SYSCALL_IMPL(sys_sched_getattr)
CREATE_STUB_SYSCALL_IMPL(sys_renameat2)
CREATE_STUB_SYSCALL_IMPL(sys_seccomp)
CREATE_STUB_SYSCALL_IMPL(sys_getrandom)
CREATE_STUB_SYSCALL_IMPL(sys_memfd_create)
CREATE_STUB_SYSCALL_IMPL(sys_bpf)
CREATE_STUB_SYSCALL_IMPL(sys_execveat)
CREATE_STUB_SYSCALL_IMPL(sys_socket)
CREATE_STUB_SYSCALL_IMPL(sys_socketpair)
CREATE_STUB_SYSCALL_IMPL(sys_bind)
CREATE_STUB_SYSCALL_IMPL(sys_connect)
CREATE_STUB_SYSCALL_IMPL(sys_listen)
CREATE_STUB_SYSCALL_IMPL(sys_accept4)
CREATE_STUB_SYSCALL_IMPL(sys_getsockopt)
CREATE_STUB_SYSCALL_IMPL(sys_setsockopt)
CREATE_STUB_SYSCALL_IMPL(sys_getsockname)
CREATE_STUB_SYSCALL_IMPL(sys_getpeername)
CREATE_STUB_SYSCALL_IMPL(sys_sendto)
CREATE_STUB_SYSCALL_IMPL(sys_sendmsg)
CREATE_STUB_SYSCALL_IMPL(sys_recvfrom)
CREATE_STUB_SYSCALL_IMPL(sys_recvmsg)
CREATE_STUB_SYSCALL_IMPL(sys_shutdown)
CREATE_STUB_SYSCALL_IMPL(sys_userfaultfd)
CREATE_STUB_SYSCALL_IMPL(sys_membarrier)
CREATE_STUB_SYSCALL_IMPL(sys_mlock2)
CREATE_STUB_SYSCALL_IMPL(sys_copy_file_range)
CREATE_STUB_SYSCALL_IMPL(sys_preadv2)
CREATE_STUB_SYSCALL_IMPL(sys_pwritev2)
CREATE_STUB_SYSCALL_IMPL(sys_pkey_mprotect)
CREATE_STUB_SYSCALL_IMPL(sys_pkey_alloc)
CREATE_STUB_SYSCALL_IMPL(sys_pkey_free)
CREATE_STUB_SYSCALL_IMPL(sys_statx)
CREATE_STUB_SYSCALL_IMPL(sys_arch_prctl)
CREATE_STUB_SYSCALL_IMPL(sys_io_pgetevents_time32)
CREATE_STUB_SYSCALL_IMPL(sys_rseq)

CREATE_STUB_SYSCALL_IMPL(sys_semget)
CREATE_STUB_SYSCALL_IMPL(sys_semctl)
CREATE_STUB_SYSCALL_IMPL(sys_shmget)
CREATE_STUB_SYSCALL_IMPL(sys_shmctl)
CREATE_STUB_SYSCALL_IMPL(sys_shmat)
CREATE_STUB_SYSCALL_IMPL(sys_shmdt)
CREATE_STUB_SYSCALL_IMPL(sys_msgget)
CREATE_STUB_SYSCALL_IMPL(sys_msgsnd)
CREATE_STUB_SYSCALL_IMPL(sys_msgrcv)
CREATE_STUB_SYSCALL_IMPL(sys_msgctl)

int sys_clock_gettime(clockid_t clk_id, struct k_timespec64 *user_tp);

CREATE_STUB_SYSCALL_IMPL(sys_clock_settime)
CREATE_STUB_SYSCALL_IMPL(sys_clock_adjtime)

int sys_clock_getres(clockid_t clk_id, struct k_timespec64 *user_res);

CREATE_STUB_SYSCALL_IMPL(sys_clock_nanosleep)
CREATE_STUB_SYSCALL_IMPL(sys_timer_gettime)
CREATE_STUB_SYSCALL_IMPL(sys_timer_settime)
CREATE_STUB_SYSCALL_IMPL(sys_timerfd_gettime)
CREATE_STUB_SYSCALL_IMPL(sys_timerfd_settime)
CREATE_STUB_SYSCALL_IMPL(sys_utimensat)
CREATE_STUB_SYSCALL_IMPL(sys_pselect6_time32)
CREATE_STUB_SYSCALL_IMPL(sys_ppoll_time32)
CREATE_STUB_SYSCALL_IMPL(sys_io_pgetevents)
CREATE_STUB_SYSCALL_IMPL(sys_recvmmsg)
CREATE_STUB_SYSCALL_IMPL(sys_mq_timedsend)
CREATE_STUB_SYSCALL_IMPL(sys_mq_timedreceive)
CREATE_STUB_SYSCALL_IMPL(sys_semtimedop)
CREATE_STUB_SYSCALL_IMPL(sys_rt_sigtimedwait)
CREATE_STUB_SYSCALL_IMPL(sys_futex)
CREATE_STUB_SYSCALL_IMPL(sys_sched_rr_get_interval)
CREATE_STUB_SYSCALL_IMPL(sys_pidfd_send_signal)
CREATE_STUB_SYSCALL_IMPL(sys_io_uring_setup)
CREATE_STUB_SYSCALL_IMPL(sys_io_uring_enter)
CREATE_STUB_SYSCALL_IMPL(sys_io_uring_register)
CREATE_STUB_SYSCALL_IMPL(sys_open_tree)
CREATE_STUB_SYSCALL_IMPL(sys_move_mount)
CREATE_STUB_SYSCALL_IMPL(sys_fsopen)
CREATE_STUB_SYSCALL_IMPL(sys_fsconfig)
CREATE_STUB_SYSCALL_IMPL(sys_fsmount)
CREATE_STUB_SYSCALL_IMPL(sys_fspick)
CREATE_STUB_SYSCALL_IMPL(sys_pidfd_open)
CREATE_STUB_SYSCALL_IMPL(sys_clone3)
CREATE_STUB_SYSCALL_IMPL(sys_close_range)
CREATE_STUB_SYSCALL_IMPL(sys_openat2)
CREATE_STUB_SYSCALL_IMPL(sys_pidfd_getfd)
CREATE_STUB_SYSCALL_IMPL(sys_faccessat2)
CREATE_STUB_SYSCALL_IMPL(sys_process_madvise)
CREATE_STUB_SYSCALL_IMPL(sys_epoll_pwait2)
CREATE_STUB_SYSCALL_IMPL(sys_mount_setattr)
CREATE_STUB_SYSCALL_IMPL(sys_quotactl_fd)
CREATE_STUB_SYSCALL_IMPL(sys_landlock_create_ruleset)
CREATE_STUB_SYSCALL_IMPL(sys_landlock_add_rule)
CREATE_STUB_SYSCALL_IMPL(sys_landlock_restrict_self)
CREATE_STUB_SYSCALL_IMPL(sys_memfd_secret)
CREATE_STUB_SYSCALL_IMPL(sys_process_mrelease)

int sys_tilck_cmd(int cmd_n, ulong a1, ulong a2, ulong a3, ulong a4);
