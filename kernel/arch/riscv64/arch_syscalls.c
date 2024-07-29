/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_tracing.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/signal.h>
#include <tilck/mods/tracing.h>

typedef long (*syscall_type)();

#define SYSFL_NO_TRACE                      0b00000001
#define SYSFL_NO_SIG                        0b00000010
#define SYSFL_NO_PREEMPT                    0b00000100
#define SYSFL_RAW_REGS                      0b00001000

struct syscall {

   union {
      void *func;
      syscall_type fptr;
   };

   u32 flags;
};

static void unknown_syscall_int(regs_t *r, u32 sn)
{
   trace_printk(5, "Unknown syscall %i", (int)sn);
   r->a0 = (ulong) -ENOSYS;
}

static void __unknown_syscall(void)
{
   struct task *curr = get_curr_task();
   regs_t *r = curr->state_regs;
   const u32 sn = r->a7;
   unknown_syscall_int(r, sn);
}

static long sys_riscv_flush_icache(ulong start, ulong end, ulong flags)
{
   /* Check the reserved flags. */
   if (UNLIKELY(flags & ~(1UL)))
      return -EINVAL;

   /* TODO: implement riscv_flush_icache() */
   return 0;
}

#define DECL_SYS(func, flags) { {func}, flags }
#define DECL_UNKNOWN_SYSCALL  DECL_SYS(__unknown_syscall, 0)

static struct syscall syscalls[MAX_SYSCALLS] =
{
   [0] = DECL_SYS(sys_io_setup, 0),
   [1] = DECL_SYS(sys_io_destroy, 0),
   [2] = DECL_SYS(sys_io_submit, 0),
   [3] = DECL_SYS(sys_io_cancel, 0),
   [4] = DECL_SYS(sys_io_getevents, 0),
   [5] = DECL_SYS(sys_setxattr, 0),
   [6] = DECL_SYS(sys_lsetxattr, 0),
   [7] = DECL_SYS(sys_fsetxattr, 0),
   [8] = DECL_SYS(sys_getxattr, 0),
   [9] = DECL_SYS(sys_lgetxattr, 0),
   [10] = DECL_SYS(sys_fgetxattr, 0),
   [11] = DECL_SYS(sys_listxattr, 0),
   [12] = DECL_SYS(sys_llistxattr, 0),
   [13] = DECL_SYS(sys_flistxattr, 0),
   [14] = DECL_SYS(sys_removexattr, 0),
   [15] = DECL_SYS(sys_lremovexattr, 0),
   [16] = DECL_SYS(sys_fremovexattr, 0),
   [17] = DECL_SYS(sys_getcwd, 0),
   [18] = DECL_SYS(sys_lookup_dcookie, 0),
   [19] = DECL_SYS(sys_eventfd2, 0),
   [20] = DECL_SYS(sys_epoll_create1, 0),
   [21] = DECL_SYS(sys_epoll_ctl, 0),
   [22] = DECL_SYS(sys_epoll_pwait, 0),
   [23] = DECL_SYS(sys_dup, 0),
   [24] = DECL_SYS(sys_dup3, 0),
   [25] = DECL_SYS(sys_fcntl64, 0),
   [26] = DECL_SYS(sys_inotify_init1, 0),
   [27] = DECL_SYS(sys_inotify_add_watch, 0),
   [28] = DECL_SYS(sys_inotify_rm_watch, 0),
   [29] = DECL_SYS(sys_ioctl, 0),
   [30] = DECL_SYS(sys_ioprio_set, 0),
   [31] = DECL_SYS(sys_ioprio_get, 0),
   [32] = DECL_SYS(sys_flock, 0),
   [33] = DECL_SYS(sys_mknodat, 0),
   [34] = DECL_SYS(sys_mkdirat, 0),
   [35] = DECL_SYS(sys_unlinkat, 0),
   [36] = DECL_SYS(sys_symlinkat, 0),
   [37] = DECL_SYS(sys_linkat, 0),
   [39] = DECL_SYS(sys_umount2, 0),
   [40] = DECL_SYS(sys_mount, 0),
   [41] = DECL_SYS(sys_pivot_root, 0),
   [42] = DECL_SYS(sys_nfsservctl, 0),
   [43] = DECL_SYS(sys_statfs, 0),
   [44] = DECL_SYS(sys_fstatfs, 0),
   [45] = DECL_SYS(sys_ia32_truncate64, 0),
   [46] = DECL_SYS(sys_ia32_ftruncate64, 0),
   [47] = DECL_SYS(sys_fallocate, 0),
   [48] = DECL_SYS(sys_faccessat, 0),
   [49] = DECL_SYS(sys_chdir, 0),
   [50] = DECL_SYS(sys_fchdir, 0),
   [51] = DECL_SYS(sys_chroot, 0),
   [52] = DECL_SYS(sys_fchmod, 0),
   [53] = DECL_SYS(sys_fchmodat, 0),
   [54] = DECL_SYS(sys_fchownat, 0),
   [55] = DECL_SYS(sys_fchown, 0),
   [56] = DECL_SYS(sys_openat, 0),
   [57] = DECL_SYS(sys_close, 0),
   [58] = DECL_SYS(sys_vhangup, 0),
   [59] = DECL_SYS(sys_pipe2, 0),
   [60] = DECL_SYS(sys_quotactl, 0),
   [61] = DECL_SYS(sys_getdents64, 0),
   [62] = DECL_SYS(sys_lseek, 0),
   [63] = DECL_SYS(sys_read, 0),
   [64] = DECL_SYS(sys_write, 0),
   [65] = DECL_SYS(sys_readv, 0),
   [66] = DECL_SYS(sys_writev, 0),
   [67] = DECL_SYS(sys_pread64, 0),
   [68] = DECL_SYS(sys_pwrite64, 0),
   [69] = DECL_SYS(sys_preadv, 0),
   [70] = DECL_SYS(sys_pwritev, 0),
   [71] = DECL_SYS(sys_sendfile, 0),
   [72] = DECL_SYS(sys_pselect6, 0),
   [73] = DECL_SYS(sys_ppoll, 0),
   [74] = DECL_SYS(sys_signalfd4, 0),
   [75] = DECL_SYS(sys_vmsplice, 0),
   [76] = DECL_SYS(sys_splice, 0),
   [77] = DECL_SYS(sys_tee, 0),
   [78] = DECL_SYS(sys_readlinkat, 0),
   [79] = DECL_SYS(sys_fstatat64, 0),
   [80] = DECL_SYS(sys_fstat64, 0),
   [81] = DECL_SYS(sys_sync, 0),
   [82] = DECL_SYS(sys_fsync, 0),
   [83] = DECL_SYS(sys_fdatasync, 0),
   [84] = DECL_SYS(sys_ia32_sync_file_range, 0),
   [85] = DECL_SYS(sys_timerfd_create, 0),
   [86] = DECL_SYS(sys_timerfd_settime, 0),
   [87] = DECL_SYS(sys_timerfd_gettime, 0),
   [88] = DECL_SYS(sys_utimensat, 0),
   [89] = DECL_SYS(sys_acct, 0),
   [90] = DECL_SYS(sys_capget, 0),
   [91] = DECL_SYS(sys_capset, 0),
   [92] = DECL_SYS(sys_personality, 0),
   [93] = DECL_SYS(sys_exit, 0),
   [94] = DECL_SYS(sys_exit_group, 0),
   [95] = DECL_SYS(sys_waitid, 0),
   [96] = DECL_SYS(sys_set_tid_address, 0),
   [97] = DECL_SYS(sys_unshare, 0),
   [98] = DECL_SYS(sys_futex, 0),
   [99] = DECL_SYS(sys_set_robust_list, 0),
   [100] = DECL_SYS(sys_get_robust_list, 0),
   [101] = DECL_SYS(sys_nanosleep, 0),
   [102] = DECL_SYS(sys_getitimer, 0),
   [103] = DECL_SYS(sys_setitimer, 0),
   [104] = DECL_SYS(sys_kexec_load, 0),
   [105] = DECL_SYS(sys_init_module, 0),
   [106] = DECL_SYS(sys_delete_module, 0),
   [107] = DECL_SYS(sys_timer_create, 0),
   [108] = DECL_SYS(sys_timer_gettime, 0),
   [109] = DECL_SYS(sys_timer_getoverrun, 0),
   [110] = DECL_SYS(sys_timer_settime, 0),
   [111] = DECL_SYS(sys_timer_delete, 0),
   [112] = DECL_SYS(sys_clock_settime, 0),
   [113] = DECL_SYS(sys_clock_gettime, 0),
   [114] = DECL_SYS(sys_clock_getres, 0),
   [115] = DECL_SYS(sys_clock_nanosleep, 0),
   [116] = DECL_SYS(sys_syslog, 0),
   [117] = DECL_SYS(sys_ptrace, 0),
   [118] = DECL_SYS(sys_sched_setparam, 0),
   [119] = DECL_SYS(sys_sched_setscheduler, 0),
   [120] = DECL_SYS(sys_sched_getscheduler, 0),
   [121] = DECL_SYS(sys_sched_getparam, 0),
   [122] = DECL_SYS(sys_sched_setaffinity, 0),
   [123] = DECL_SYS(sys_sched_getaffinity, 0),
   [124] = DECL_SYS(sys_sched_yield, 0),
   [125] = DECL_SYS(sys_sched_get_priority_max, 0),
   [126] = DECL_SYS(sys_sched_set_priority_min, 0),
   [127] = DECL_SYS(sys_sched_rr_get_interval, 0),
   [128] = DECL_SYS(sys_restart_syscall, 0),
   [129] = DECL_SYS(sys_kill, 0),
   [130] = DECL_SYS(sys_tkill, 0),
   [131] = DECL_SYS(sys_tgkill, 0),
   [132] = DECL_SYS(sys_sigaltstack, 0),
   [133] = DECL_SYS(sys_rt_sigsuspend, SYSFL_NO_PREEMPT),
   [134] = DECL_SYS(sys_rt_sigaction, 0),
   [135] = DECL_SYS(sys_rt_sigprocmask, SYSFL_NO_PREEMPT),
   [136] = DECL_SYS(sys_rt_sigpending, SYSFL_NO_PREEMPT),
   [137] = DECL_SYS(sys_rt_sigtimedwait, 0),
   [138] = DECL_SYS(sys_rt_sigqueueinfo, 0),
   [139] = DECL_SYS(
      sys_rt_sigreturn,
      0 | SYSFL_NO_TRACE | SYSFL_NO_SIG | SYSFL_NO_PREEMPT
   ),
   [140] = DECL_SYS(sys_setpriority, 0),
   [141] = DECL_SYS(sys_getpriority, 0),
   [142] = DECL_SYS(sys_reboot, 0),
   [143] = DECL_SYS(sys_setregid, 0),
   [144] = DECL_SYS(sys_setgid, 0),
   [145] = DECL_SYS(sys_setreuid, 0),
   [146] = DECL_SYS(sys_setuid, 0),
   [147] = DECL_SYS(sys_setresuid, 0),
   [148] = DECL_SYS(sys_getresuid, 0),
   [149] = DECL_SYS(sys_setresgid, 0),
   [150] = DECL_SYS(sys_getresgid, 0),
   [151] = DECL_SYS(sys_setfsuid, 0),
   [152] = DECL_SYS(sys_setfsgid, 0),
   [153] = DECL_SYS(sys_times, 0),
   [154] = DECL_SYS(sys_setpgid, 0),
   [155] = DECL_SYS(sys_getpgid, 0),
   [156] = DECL_SYS(sys_getsid, 0),
   [157] = DECL_SYS(sys_setsid, 0),
   [158] = DECL_SYS(sys_getgroups, 0),
   [159] = DECL_SYS(sys_setgroups, 0),
   [160] = DECL_SYS(sys_newuname, 0),
   [161] = DECL_SYS(sys_sethostname, 0),
   [162] = DECL_SYS(sys_setdomainname, 0),
   [163] = DECL_SYS(sys_getrlimit, 0),
   [164] = DECL_SYS(sys_setrlimit, 0),
   [165] = DECL_SYS(sys_getrusage, 0),
   [166] = DECL_SYS(sys_umask, 0),
   [167] = DECL_SYS(sys_prctl, 0),
   [168] = DECL_SYS(sys_getcpu, 0),
   [169] = DECL_SYS(sys_gettimeofday, 0),
   [170] = DECL_SYS(sys_settimeofday, 0),
   [171] = DECL_SYS(sys_adjtimex, 0),
   [172] = DECL_SYS(sys_getpid, 0),
   [173] = DECL_SYS(sys_getppid, 0),
   [174] = DECL_SYS(sys_getuid, 0),
   [175] = DECL_SYS(sys_geteuid, 0),
   [176] = DECL_SYS(sys_getgid, 0),
   [177] = DECL_SYS(sys_getegid, 0),
   [178] = DECL_SYS(sys_gettid, 0),
   [179] = DECL_SYS(sys_sysinfo, 0),
   [180] = DECL_SYS(sys_mq_open, 0),
   [181] = DECL_SYS(sys_mq_unlink, 0),
   [182] = DECL_SYS(sys_mq_timedsend, 0),
   [183] = DECL_SYS(sys_mq_timedreceive, 0),
   [184] = DECL_SYS(sys_mq_notify, 0),
   [185] = DECL_SYS(sys_mq_getsetattr, 0),
   [186] = DECL_SYS(sys_msgget, 0),
   [187] = DECL_SYS(sys_msgctl, 0),
   [188] = DECL_SYS(sys_msgrcv, 0),
   [189] = DECL_SYS(sys_msgsnd, 0),
   [190] = DECL_SYS(sys_semget, 0),
   [191] = DECL_SYS(sys_semctl, 0),
   [192] = DECL_SYS(sys_semtimedop, 0),
   [193] = DECL_SYS(sys_semop, 0),
   [194] = DECL_SYS(sys_shmget, 0),
   [195] = DECL_SYS(sys_shmctl, 0),
   [196] = DECL_SYS(sys_shmat, 0),
   [197] = DECL_SYS(sys_shmdt, 0),
   [198] = DECL_SYS(sys_socket, 0),
   [199] = DECL_SYS(sys_socketpair, 0),
   [200] = DECL_SYS(sys_bind, 0),
   [201] = DECL_SYS(sys_listen, 0),
   [202] = DECL_SYS(sys_accept, 0),
   [203] = DECL_SYS(sys_connect, 0),
   [204] = DECL_SYS(sys_getsockname, 0),
   [205] = DECL_SYS(sys_getpeername, 0),
   [206] = DECL_SYS(sys_sendto, 0),
   [207] = DECL_SYS(sys_recvfrom, 0),
   [208] = DECL_SYS(sys_setsockopt, 0),
   [209] = DECL_SYS(sys_getsockopt, 0),
   [210] = DECL_SYS(sys_shutdown, 0),
   [211] = DECL_SYS(sys_sendmsg, 0),
   [212] = DECL_SYS(sys_recvmsg, 0),
   [213] = DECL_SYS(sys_ia32_readahead, 0),
   [214] = DECL_SYS(sys_brk, 0),
   [215] = DECL_SYS(sys_munmap, 0),
   [216] = DECL_SYS(sys_mremap, 0),
   [217] = DECL_SYS(sys_add_key, 0),
   [218] = DECL_SYS(sys_request_key, 0),
   [219] = DECL_SYS(sys_keyctl, 0),
   [220] = DECL_SYS(sys_clone, SYSFL_RAW_REGS),
   [221] = DECL_SYS(sys_execve, 0),
   [222] = DECL_SYS(sys_mmap, 0),
   [223] = DECL_SYS(sys_ia32_fadvise64, 0),
   [224] = DECL_SYS(sys_swapon, 0),
   [225] = DECL_SYS(sys_swapoff, 0),
   [226] = DECL_SYS(sys_mprotect, 0),
   [227] = DECL_SYS(sys_msync, 0),
   [228] = DECL_SYS(sys_mlock, 0),
   [229] = DECL_SYS(sys_munlock, 0),
   [230] = DECL_SYS(sys_mlockall, 0),
   [231] = DECL_SYS(sys_munlockall, 0),
   [232] = DECL_SYS(sys_mincore, 0),
   [233] = DECL_SYS(sys_madvise, 0),
   [234] = DECL_SYS(sys_remap_file_pages, 0),
   [235] = DECL_SYS(sys_mbind, 0),
   [236] = DECL_SYS(sys_get_mempolicy, 0),
   [237] = DECL_SYS(sys_set_mempolicy, 0),
   [238] = DECL_SYS(sys_migrate_pages, 0),
   [239] = DECL_SYS(sys_move_pages, 0),
   [240] = DECL_SYS(sys_rt_tgsigqueueinfo, 0),
   [241] = DECL_SYS(sys_perf_event_open, 0),
   [242] = DECL_SYS(sys_accept4, 0),
   [243] = DECL_SYS(sys_recvmmsg, 0),
   [244 ... 258] = DECL_UNKNOWN_SYSCALL,
   [259] = DECL_SYS(sys_riscv_flush_icache, 0),
   [260] = DECL_SYS(sys_wait4, 0),
   [261] = DECL_SYS(sys_prlimit64, 0),
   [262] = DECL_SYS(sys_fanotify_init, 0),
   [263] = DECL_SYS(sys_fanotify_mark, 0),
   [264] = DECL_SYS(sys_name_to_handle_at, 0),
   [265] = DECL_SYS(sys_open_by_handle_at, 0),
   [266] = DECL_SYS(sys_clock_adjtime, 0),
   [267] = DECL_SYS(sys_syncfs, 0),
   [268] = DECL_SYS(sys_setns, 0),
   [269] = DECL_SYS(sys_sendmmsg, 0),
   [270] = DECL_SYS(sys_process_vm_readv, 0),
   [271] = DECL_SYS(sys_process_vm_writev, 0),
   [272] = DECL_SYS(sys_kcmp, 0),
   [273] = DECL_SYS(sys_finit_module, 0),
   [274] = DECL_SYS(sys_sched_setattr, 0),
   [275] = DECL_SYS(sys_sched_getattr, 0),
   [276] = DECL_SYS(sys_renameat2, 0),
   [277] = DECL_SYS(sys_seccomp, 0),
   [278] = DECL_SYS(sys_getrandom, 0),
   [279] = DECL_SYS(sys_memfd_create, 0),
   [280] = DECL_SYS(sys_bpf, 0),
   [281] = DECL_SYS(sys_execveat, 0),
   [282] = DECL_SYS(sys_userfaultfd, 0),
   [283] = DECL_SYS(sys_membarrier, 0),
   [284] = DECL_SYS(sys_mlock2, 0),
   [285] = DECL_SYS(sys_copy_file_range, 0),
   [286] = DECL_SYS(sys_preadv2, 0),
   [287] = DECL_SYS(sys_pwritev2, 0),
   [288] = DECL_SYS(sys_pkey_mprotect, 0),
   [289] = DECL_SYS(sys_pkey_alloc, 0),
   [290] = DECL_SYS(sys_pkey_free, 0),
   [291] = DECL_SYS(sys_statx, 0),
   [292] = DECL_SYS(sys_io_pgetevents, 0),
   [293] = DECL_SYS(sys_rseq, 0),
   [294] = DECL_SYS(sys_kexec_file_load, 0),
   [295 ... 423] = DECL_UNKNOWN_SYSCALL,
   [424] = DECL_SYS(sys_pidfd_send_signal, 0),
   [425] = DECL_SYS(sys_io_uring_setup, 0),
   [426] = DECL_SYS(sys_io_uring_enter, 0),
   [427] = DECL_SYS(sys_io_uring_register, 0),
   [428] = DECL_SYS(sys_open_tree, 0),
   [429] = DECL_SYS(sys_move_mount, 0),
   [430] = DECL_SYS(sys_fsopen, 0),
   [431] = DECL_SYS(sys_fsconfig, 0),
   [432] = DECL_SYS(sys_fsmount, 0),
   [433] = DECL_SYS(sys_fspick, 0),
   [434] = DECL_SYS(sys_pidfd_open, 0),
   [435] = DECL_SYS(sys_clone3, 0),
   [436] = DECL_SYS(sys_close_range, 0),
   [437] = DECL_SYS(sys_openat2, 0),
   [438] = DECL_SYS(sys_pidfd_getfd, 0),
   [439] = DECL_SYS(sys_faccessat2, 0),
   [440] = DECL_SYS(sys_process_madvise, 0),
   [441] = DECL_SYS(sys_epoll_pwait2, 0),
   [442] = DECL_SYS(sys_mount_setattr, 0),
   [443] = DECL_UNKNOWN_SYSCALL,
   [444] = DECL_SYS(sys_landlock_create_ruleset, 0),
   [445] = DECL_SYS(sys_landlock_add_rule, 0),
   [446] = DECL_SYS(sys_landlock_restrict_self, 0),
   [447] = DECL_UNKNOWN_SYSCALL,
   [448] = DECL_SYS(sys_process_mrelease, 0),
   [449] = DECL_SYS(sys_futex_waitv, 0),
   [450] = DECL_SYS(sys_set_mempolicy_home_node, 0),
   [451] = DECL_SYS(sys_cachestat, 0),
   [452] = DECL_SYS(sys_fchmodat2, 0),
   [453 ... (TILCK_CMD_SYSCALL - 1)] = DECL_UNKNOWN_SYSCALL,

   [TILCK_CMD_SYSCALL] = DECL_SYS(sys_tilck_cmd, 0),
};

void *get_syscall_func_ptr(u32 n)
{
   if (n >= ARRAY_SIZE(syscalls))
      return NULL;

   return syscalls[n].func;
}

int get_syscall_num(void *func)
{
   if (!func)
      return -1;

   for (int i = 0; i < ARRAY_SIZE(syscalls); i++)
      if (syscalls[i].func == func)
         return i;

   return -1;
}

static NO_INLINE void
do_syscall_int(syscall_type fptr, regs_t *r, bool raw_regs)
{
   if (LIKELY(!raw_regs))
      r->a0 = fptr(r->a0,r->a1,r->a2,r->a3,r->a4,r->a5);
   else
      r->a0 = fptr(r, r->a0,r->a1,r->a2,r->a3,r->a4,r->a5);
}

static void do_special_syscall(regs_t *r)
{
   struct task *curr = get_curr_task();
   const u32 sn = r->a7;
   const u32 fl = syscalls[sn].flags;
   const syscall_type fptr = syscalls[sn].fptr;
   const bool signals = ~fl & SYSFL_NO_SIG;
   const bool preemptable = ~fl & SYSFL_NO_PREEMPT;
   const bool traceable = ~fl & SYSFL_NO_TRACE;
   const bool raw_regs = fl & SYSFL_RAW_REGS;

   if (signals)
      process_signals(curr, sig_pre_syscall, r);

   if (preemptable)
      enable_preemption();

   if (traceable)
      trace_sys_enter(sn,r->a0,r->a1,r->a2,r->a3,r->a4,r->a5);

   do_syscall_int(fptr, r, raw_regs);

   if (traceable)
      trace_sys_exit(sn,r->a0,r->a1,r->a2,r->a3,r->a4,r->a5, r->a7);

   if (preemptable)
      disable_preemption();

   if (signals)
      process_signals(curr, sig_in_syscall, r);
}

static void do_syscall(regs_t *r)
{
   struct task *curr = get_curr_task();
   const u32 sn = r->a7;
   const syscall_type fptr = syscalls[sn].fptr;

   process_signals(curr, sig_pre_syscall, r);
   enable_preemption();
   {
      trace_sys_enter(sn,r->a0,r->a1,r->a2,r->a3,r->a4,r->a5);
      do_syscall_int(fptr, r, false);
      trace_sys_exit(sn,r->a0,r->a1,r->a2,r->a3,r->a4,r->a5, r->a7);
   }
   disable_preemption();
   process_signals(curr, sig_in_syscall, r);
}

void handle_syscall(regs_t *r)
{
   const u32 sn = r->a7;

   /*
    * Advance SEPC 0x4 to avoid executing the original
    * scall instruction after sret.
    */
   r->sepc += 0x4;

   save_current_task_state(r, false);
   set_current_task_in_kernel();

   if (LIKELY(sn < ARRAY_SIZE(syscalls))) {

      if (LIKELY(syscalls[sn].flags == 0))
         do_syscall(r);
      else
         do_special_syscall(r);

   } else {

      unknown_syscall_int(r, sn);
   }

   set_current_task_in_user_mode();
}

void init_syscall_interfaces(void)
{
   /* do nothing */
}

