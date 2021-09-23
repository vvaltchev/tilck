/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_tracing.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/signal.h>
#include <tilck/mods/tracing.h>

#include "idt_int.h"

void syscall_int80_entry(void);
void sysenter_entry(void);

typedef long (*syscall_type)();

#define SYSFL_NO_TRACE                      0b00000001
#define SYSFL_NO_SIG                        0b00000010
#define SYSFL_NO_PREEMPT                    0b00000100

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
   r->eax = (ulong) -ENOSYS;
}

static void __unknown_syscall(void)
{
   struct task *curr = get_curr_task();
   regs_t *r = curr->state_regs;
   const u32 sn = r->eax;
   unknown_syscall_int(r, sn);
}

#define DECL_SYS(func, flags) { {func}, flags }
#define DECL_UNKNOWN_SYSCALL  DECL_SYS(__unknown_syscall, 0)

/*
 * The syscall numbers are ARCH-dependent
 *
 * The numbers and the syscall names MUST BE in sync with the following file
 * in the Linux kernel:
 *
 *    arch/x86/entry/syscalls/syscall_32.tbl
 *
 * Lasy synced with Linux 5.15-rc2.
 */
static struct syscall syscalls[MAX_SYSCALLS] =
{
   [0] = DECL_SYS(sys_restart_syscall, 0),
   [1] = DECL_SYS(sys_exit, 0),
   [2] = DECL_SYS(sys_fork, 0),
   [3] = DECL_SYS(sys_read, 0),
   [4] = DECL_SYS(sys_write, 0),
   [5] = DECL_SYS(sys_open, 0),
   [6] = DECL_SYS(sys_close, 0),
   [7] = DECL_SYS(sys_waitpid, 0),
   [8] = DECL_SYS(sys_creat, 0),
   [9] = DECL_SYS(sys_link, 0),
   [10] = DECL_SYS(sys_unlink, 0),
   [11] = DECL_SYS(sys_execve, 0),
   [12] = DECL_SYS(sys_chdir, 0),
   [13] = DECL_SYS(sys_time, 0),
   [14] = DECL_SYS(sys_mknod, 0),
   [15] = DECL_SYS(sys_chmod, 0),
   [16] = DECL_SYS(sys_lchown16, 0),
   [17] = DECL_UNKNOWN_SYSCALL,
   [18] = DECL_SYS(sys_oldstat, 0),
   [19] = DECL_SYS(sys_lseek, 0),
   [20] = DECL_SYS(sys_getpid, 0),
   [21] = DECL_SYS(sys_mount, 0),
   [22] = DECL_SYS(sys_oldumount, 0),
   [23] = DECL_SYS(sys_setuid16, 0),
   [24] = DECL_SYS(sys_getuid16, 0),
   [25] = DECL_SYS(sys_stime32, 0),
   [26] = DECL_SYS(sys_ptrace, 0),
   [27] = DECL_SYS(sys_alarm, 0),
   [28] = DECL_SYS(sys_oldfstat, 0),
   [29] = DECL_SYS(sys_pause, SYSFL_NO_PREEMPT),
   [30] = DECL_SYS(sys_utime32, 0),
   [31] = DECL_UNKNOWN_SYSCALL,
   [32] = DECL_UNKNOWN_SYSCALL,
   [33] = DECL_SYS(sys_access, 0),
   [34] = DECL_SYS(sys_nice, 0),
   [35] = DECL_UNKNOWN_SYSCALL,
   [36] = DECL_SYS(sys_sync, 0),
   [37] = DECL_SYS(sys_kill, 0),
   [38] = DECL_SYS(sys_rename, 0),
   [39] = DECL_SYS(sys_mkdir, 0),
   [40] = DECL_SYS(sys_rmdir, 0),
   [41] = DECL_SYS(sys_dup, 0),
   [42] = DECL_SYS(sys_pipe, 0),
   [43] = DECL_SYS(sys_times, 0),
   [44] = DECL_UNKNOWN_SYSCALL,
   [45] = DECL_SYS(sys_brk, 0),
   [46] = DECL_SYS(sys_setgid16, 0),
   [47] = DECL_SYS(sys_getgid16, 0),
   [48] = DECL_SYS(sys_signal, 0),
   [49] = DECL_SYS(sys_geteuid16, 0),
   [50] = DECL_SYS(sys_getegid16, 0),
   [51] = DECL_SYS(sys_acct, 0),
   [52] = DECL_SYS(sys_umount, 0),
   [53] = DECL_UNKNOWN_SYSCALL,
   [54] = DECL_SYS(sys_ioctl, 0),
   [55] = DECL_SYS(sys_fcntl, 0),
   [56] = DECL_UNKNOWN_SYSCALL,
   [57] = DECL_SYS(sys_setpgid, 0),
   [58] = DECL_UNKNOWN_SYSCALL,
   [59] = DECL_SYS(sys_olduname, 0),
   [60] = DECL_SYS(sys_umask, 0),
   [61] = DECL_SYS(sys_chroot, 0),
   [62] = DECL_SYS(sys_ustat, 0),
   [63] = DECL_SYS(sys_dup2, 0),
   [64] = DECL_SYS(sys_getppid, 0),
   [65] = DECL_SYS(sys_getpgrp, 0),
   [66] = DECL_SYS(sys_setsid, 0),
   [67] = DECL_SYS(sys_sigaction, 0),
   [68] = DECL_SYS(sys_sgetmask, 0),
   [69] = DECL_SYS(sys_ssetmask, 0),
   [70] = DECL_SYS(sys_setreuid16, 0),
   [71] = DECL_SYS(sys_setregid16, 0),
   [72] = DECL_SYS(sys_sigsuspend, 0),
   [73] = DECL_SYS(sys_sigpending, 0),
   [74] = DECL_SYS(sys_sethostname, 0),
   [75] = DECL_SYS(sys_setrlimit, 0),
   [76] = DECL_SYS(sys_old_getrlimit, 0),
   [77] = DECL_SYS(sys_getrusage, 0),
   [78] = DECL_SYS(sys_gettimeofday, 0),
   [79] = DECL_SYS(sys_settimeofday, 0),
   [80] = DECL_SYS(sys_getgroups16, 0),
   [81] = DECL_SYS(sys_setgroups16, 0),
   [82] = DECL_SYS(sys_old_select, 0),
   [83] = DECL_SYS(sys_symlink, 0),
   [84] = DECL_SYS(sys_lstat, 0),
   [85] = DECL_SYS(sys_readlink, 0),
   [86] = DECL_SYS(sys_uselib, 0),
   [87] = DECL_SYS(sys_swapon, 0),
   [88] = DECL_SYS(sys_reboot, 0),
   [89] = DECL_SYS(sys_old_readdir, 0),
   [90] = DECL_SYS(sys_old_mmap, 0),
   [91] = DECL_SYS(sys_munmap, 0),
   [92] = DECL_SYS(sys_truncate, 0),
   [93] = DECL_SYS(sys_ftruncate, 0),
   [94] = DECL_SYS(sys_fchmod, 0),
   [95] = DECL_SYS(sys_fchown16, 0),
   [96] = DECL_SYS(sys_getpriority, 0),
   [97] = DECL_SYS(sys_setpriority, 0),
   [98] = DECL_UNKNOWN_SYSCALL,
   [99] = DECL_SYS(sys_statfs, 0),
   [100] = DECL_SYS(sys_fstatfs, 0),
   [101] = DECL_SYS(sys_ioperm, 0),
   [102] = DECL_SYS(sys_socketcall, 0),
   [103] = DECL_SYS(sys_syslog, 0),
   [104] = DECL_SYS(sys_setitimer, 0),
   [105] = DECL_SYS(sys_getitimer, 0),
   [106] = DECL_SYS(sys_newstat, 0),
   [107] = DECL_SYS(sys_newlstat, 0),
   [108] = DECL_SYS(sys_newfstat, 0),
   [109] = DECL_SYS(sys_uname, 0),
   [110] = DECL_SYS(sys_iopl, 0),
   [111] = DECL_SYS(sys_vhangup, 0),
   [112] = DECL_UNKNOWN_SYSCALL,
   [113] = DECL_SYS(sys_vm86old, 0),
   [114] = DECL_SYS(sys_wait4, 0),
   [115] = DECL_SYS(sys_swapoff, 0),
   [116] = DECL_SYS(sys_sysinfo, 0),
   [117] = DECL_SYS(sys_ipc, 0),
   [118] = DECL_SYS(sys_fsync, 0),
   [119] = DECL_SYS(sys_sigreturn, 0),
   [120] = DECL_SYS(sys_clone, 0),
   [121] = DECL_SYS(sys_setdomainname, 0),
   [122] = DECL_SYS(sys_newuname, 0),
   [123] = DECL_SYS(sys_modify_ldt, 0),
   [124] = DECL_SYS(sys_adjtimex_time32, 0),
   [125] = DECL_SYS(sys_mprotect, 0),
   [126] = DECL_SYS(sys_sigprocmask, 0),
   [127] = DECL_UNKNOWN_SYSCALL,
   [128] = DECL_SYS(sys_init_module, 0),
   [129] = DECL_SYS(sys_delete_module, 0),
   [130] = DECL_UNKNOWN_SYSCALL,
   [131] = DECL_SYS(sys_quotactl, 0),
   [132] = DECL_SYS(sys_getpgid, 0),
   [133] = DECL_SYS(sys_fchdir, 0),
   [134] = DECL_SYS(sys_bdflush, 0),
   [135] = DECL_SYS(sys_sysfs, 0),
   [136] = DECL_SYS(sys_personality, 0),
   [137] = DECL_UNKNOWN_SYSCALL,
   [138] = DECL_SYS(sys_setfsuid16, 0),
   [139] = DECL_SYS(sys_setfsgid16, 0),
   [140] = DECL_SYS(sys_llseek, 0),
   [141] = DECL_SYS(sys_getdents, 0),
   [142] = DECL_SYS(sys_select, 0),
   [143] = DECL_SYS(sys_flock, 0),
   [144] = DECL_SYS(sys_msync, 0),
   [145] = DECL_SYS(sys_readv, 0),
   [146] = DECL_SYS(sys_writev, 0),
   [147] = DECL_SYS(sys_getsid, 0),
   [148] = DECL_SYS(sys_fdatasync, 0),
   [149] = DECL_SYS(sys_sysctl, 0),
   [150] = DECL_SYS(sys_mlock, 0),
   [151] = DECL_SYS(sys_munlock, 0),
   [152] = DECL_SYS(sys_mlockall, 0),
   [153] = DECL_SYS(sys_munlockall, 0),
   [154] = DECL_SYS(sys_sched_setparam, 0),
   [155] = DECL_SYS(sys_sched_getparam, 0),
   [156] = DECL_SYS(sys_sched_setscheduler, 0),
   [157] = DECL_SYS(sys_sched_getscheduler, 0),
   [158] = DECL_SYS(sys_sched_yield, 0),
   [159] = DECL_SYS(sys_sched_get_priority_max, 0),
   [160] = DECL_SYS(sys_sched_set_priority_min, 0),
   [161] = DECL_SYS(sys_sched_rr_get_interval_time32, 0),
   [162] = DECL_SYS(sys_nanosleep_time32, 0),
   [163] = DECL_SYS(sys_mremap, 0),
   [164] = DECL_SYS(sys_setresuid16, 0),
   [165] = DECL_SYS(sys_getresuid16, 0),
   [166] = DECL_SYS(sys_vm86, 0),
   [167] = DECL_UNKNOWN_SYSCALL,
   [168] = DECL_SYS(sys_poll, 0),
   [169] = DECL_SYS(sys_nfsservctl, 0),
   [170] = DECL_SYS(sys_setresgid16, 0),
   [171] = DECL_SYS(sys_getresgid16, 0),
   [172] = DECL_SYS(sys_prctl, 0),
   [173] = DECL_SYS(
      sys_rt_sigreturn,
      0 | SYSFL_NO_TRACE | SYSFL_NO_SIG | SYSFL_NO_PREEMPT
   ),
   [174] = DECL_SYS(sys_rt_sigaction, 0),
   [175] = DECL_SYS(sys_rt_sigprocmask, SYSFL_NO_PREEMPT),
   [176] = DECL_SYS(sys_rt_sigpending, SYSFL_NO_PREEMPT),
   [177] = DECL_SYS(sys_rt_sigtimedwait_time32, 0),
   [178] = DECL_SYS(sys_rt_sigqueueinfo, 0),
   [179] = DECL_SYS(sys_rt_sigsuspend, SYSFL_NO_PREEMPT),
   [180] = DECL_SYS(sys_pread64, 0),
   [181] = DECL_SYS(sys_pwrite64, 0),
   [182] = DECL_SYS(sys_chown16, 0),
   [183] = DECL_SYS(sys_getcwd, 0),
   [184] = DECL_SYS(sys_capget, 0),
   [185] = DECL_SYS(sys_capset, 0),
   [186] = DECL_SYS(sys_sigaltstack, 0),
   [187] = DECL_SYS(sys_sendfile, 0),
   [188] = DECL_UNKNOWN_SYSCALL,
   [189] = DECL_UNKNOWN_SYSCALL,
   [190] = DECL_SYS(sys_vfork, 0),
   [191] = DECL_SYS(sys_getrlimit, 0),
   [192] = DECL_SYS(sys_mmap_pgoff, 0),
   [193] = DECL_SYS(sys_ia32_truncate64, 0),
   [194] = DECL_SYS(sys_ia32_ftruncate64, 0),
   [195] = DECL_SYS(sys_stat64, 0),
   [196] = DECL_SYS(sys_lstat64, 0),
   [197] = DECL_SYS(sys_fstat64, 0),
   [198] = DECL_SYS(sys_lchown, 0),
   [199] = DECL_SYS(sys_getuid, 0),
   [200] = DECL_SYS(sys_getgid, 0),
   [201] = DECL_SYS(sys_geteuid, 0),
   [202] = DECL_SYS(sys_getegid, 0),
   [203] = DECL_SYS(sys_setreuid, 0),
   [204] = DECL_SYS(sys_setregid, 0),
   [205] = DECL_SYS(sys_getgroups, 0),
   [206] = DECL_SYS(sys_setgroups, 0),
   [207] = DECL_SYS(sys_fchown, 0),
   [208] = DECL_SYS(sys_setresuid, 0),
   [209] = DECL_SYS(sys_getresuid, 0),
   [210] = DECL_SYS(sys_setresgid, 0),
   [211] = DECL_SYS(sys_getresgid, 0),
   [212] = DECL_SYS(sys_chown, 0),
   [213] = DECL_SYS(sys_setuid, 0),
   [214] = DECL_SYS(sys_setgid, 0),
   [215] = DECL_SYS(sys_setfsuid, 0),
   [216] = DECL_SYS(sys_setfsgid, 0),
   [217] = DECL_SYS(sys_pivot_root, 0),
   [218] = DECL_SYS(sys_mincore, 0),
   [219] = DECL_SYS(sys_madvise, 0),
   [220] = DECL_SYS(sys_getdents64, 0),
   [221] = DECL_SYS(sys_fcntl64, 0),
   [222] = DECL_UNKNOWN_SYSCALL,
   [223] = DECL_UNKNOWN_SYSCALL,
   [224] = DECL_SYS(sys_gettid, 0),
   [225] = DECL_SYS(sys_ia32_readahead, 0),
   [226] = DECL_SYS(sys_setxattr, 0),
   [227] = DECL_SYS(sys_lsetxattr, 0),
   [228] = DECL_SYS(sys_fsetxattr, 0),
   [229] = DECL_SYS(sys_getxattr, 0),
   [230] = DECL_SYS(sys_lgetxattr, 0),
   [231] = DECL_SYS(sys_fgetxattr, 0),
   [232] = DECL_SYS(sys_listxattr, 0),
   [233] = DECL_SYS(sys_llistxattr, 0),
   [234] = DECL_SYS(sys_flistxattr, 0),
   [235] = DECL_SYS(sys_removexattr, 0),
   [236] = DECL_SYS(sys_lremovexattr, 0),
   [237] = DECL_SYS(sys_fremovexattr, 0),
   [238] = DECL_SYS(sys_tkill, 0),
   [239] = DECL_SYS(sys_sendfile64, 0),
   [240] = DECL_SYS(sys_futex_time32, 0),
   [241] = DECL_SYS(sys_sched_setaffinity, 0),
   [242] = DECL_SYS(sys_sched_getaffinity, 0),
   [243] = DECL_SYS(sys_set_thread_area, 0),
   [244] = DECL_SYS(sys_get_thread_area, 0),
   [245] = DECL_SYS(sys_io_setup, 0),
   [246] = DECL_SYS(sys_io_destroy, 0),
   [247] = DECL_SYS(sys_io_getevents_time32, 0),
   [248] = DECL_SYS(sys_io_submit, 0),
   [249] = DECL_SYS(sys_io_cancel, 0),
   [250] = DECL_SYS(sys_ia32_fadvise64, 0),
   [251] = DECL_UNKNOWN_SYSCALL,
   [252] = DECL_SYS(sys_exit_group, 0),
   [253] = DECL_SYS(sys_lookup_dcookie, 0),
   [254] = DECL_SYS(sys_epoll_create, 0),
   [255] = DECL_SYS(sys_epoll_ctl, 0),
   [256] = DECL_SYS(sys_epoll_wait, 0),
   [257] = DECL_SYS(sys_remap_file_pages, 0),
   [258] = DECL_SYS(sys_set_tid_address, 0),
   [259] = DECL_SYS(sys_timer_create, 0),
   [260] = DECL_SYS(sys_timer_settime32, 0),
   [261] = DECL_SYS(sys_timer_gettime32, 0),
   [262] = DECL_SYS(sys_timer_getoverrun, 0),
   [263] = DECL_SYS(sys_timer_delete, 0),
   [264] = DECL_SYS(sys_clock_settime32, 0),
   [265] = DECL_SYS(sys_clock_gettime32, 0),
   [266] = DECL_SYS(sys_clock_getres_time32, 0),
   [267] = DECL_SYS(sys_clock_nanosleep_time32, 0),
   [268] = DECL_SYS(sys_statfs64, 0),
   [269] = DECL_SYS(sys_fstatfs64, 0),
   [270] = DECL_SYS(sys_tgkill, 0),
   [271] = DECL_SYS(sys_utimes, 0),
   [272] = DECL_SYS(sys_ia32_fadvise64_64, 0),
   [273] = DECL_UNKNOWN_SYSCALL,
   [274] = DECL_SYS(sys_mbind, 0),
   [275] = DECL_SYS(sys_get_mempolicy, 0),
   [276] = DECL_SYS(sys_set_mempolicy, 0),
   [277] = DECL_SYS(sys_mq_open, 0),
   [278] = DECL_SYS(sys_mq_unlink, 0),
   [279] = DECL_SYS(sys_mq_timedsend_time32, 0),
   [280] = DECL_SYS(sys_mq_timedreceive_time32, 0),
   [281] = DECL_SYS(sys_mq_notify, 0),
   [282] = DECL_SYS(sys_mq_getsetattr, 0),
   [283] = DECL_SYS(sys_kexec_load, 0),
   [284] = DECL_SYS(sys_waitid, 0),
   [285] = DECL_UNKNOWN_SYSCALL,
   [286] = DECL_SYS(sys_add_key, 0),
   [287] = DECL_SYS(sys_request_key, 0),
   [288] = DECL_SYS(sys_keyctl, 0),
   [289] = DECL_SYS(sys_ioprio_set, 0),
   [290] = DECL_SYS(sys_ioprio_get, 0),
   [291] = DECL_SYS(sys_inotify_init, 0),
   [292] = DECL_SYS(sys_inotify_add_watch, 0),
   [293] = DECL_SYS(sys_inotify_rm_watch, 0),
   [294] = DECL_SYS(sys_migrate_pages, 0),
   [295] = DECL_SYS(sys_openat, 0),
   [296] = DECL_SYS(sys_mkdirat, 0),
   [297] = DECL_SYS(sys_mknodat, 0),
   [298] = DECL_SYS(sys_fchownat, 0),
   [299] = DECL_SYS(sys_futimesat_time32, 0),
   [300] = DECL_SYS(sys_fstatat64, 0),
   [301] = DECL_SYS(sys_unlinkat, 0),
   [302] = DECL_SYS(sys_renameat, 0),
   [303] = DECL_SYS(sys_linkat, 0),
   [304] = DECL_SYS(sys_symlinkat, 0),
   [305] = DECL_SYS(sys_readlinkat, 0),
   [306] = DECL_SYS(sys_fchmodat, 0),
   [307] = DECL_SYS(sys_faccessat, 0),
   [308] = DECL_SYS(sys_pselect6_time32, 0),
   [309] = DECL_SYS(sys_ppoll_time32, 0),
   [310] = DECL_SYS(sys_unshare, 0),
   [311] = DECL_SYS(sys_set_robust_list, 0),
   [312] = DECL_SYS(sys_get_robust_list, 0),
   [313] = DECL_SYS(sys_splice, 0),
   [314] = DECL_SYS(sys_ia32_sync_file_range, 0),
   [315] = DECL_SYS(sys_tee, 0),
   [316] = DECL_SYS(sys_vmsplice, 0),
   [317] = DECL_SYS(sys_move_pages, 0),
   [318] = DECL_SYS(sys_getcpu, 0),
   [319] = DECL_SYS(sys_epoll_pwait, 0),
   [320] = DECL_SYS(sys_utimensat_time32, 0),
   [321] = DECL_SYS(sys_signalfd, 0),
   [322] = DECL_SYS(sys_timerfd_create, 0),
   [323] = DECL_SYS(sys_eventfd, 0),
   [324] = DECL_SYS(sys_fallocate, 0),
   [325] = DECL_SYS(sys_timerfd_settime32, 0),
   [326] = DECL_SYS(sys_timerfd_gettime32, 0),
   [327] = DECL_SYS(sys_signalfd4, 0),
   [328] = DECL_SYS(sys_eventfd2, 0),
   [329] = DECL_SYS(sys_epoll_create1, 0),
   [330] = DECL_SYS(sys_dup3, 0),
   [331] = DECL_SYS(sys_pipe2, 0),
   [332] = DECL_SYS(sys_inotify_init1, 0),
   [333] = DECL_SYS(sys_preadv, 0),
   [334] = DECL_SYS(sys_pwritev, 0),
   [335] = DECL_SYS(sys_rt_tgsigqueueinfo, 0),
   [336] = DECL_SYS(sys_perf_event_open, 0),
   [337] = DECL_SYS(sys_recvmmsg_time32, 0),
   [338] = DECL_SYS(sys_fanotify_init, 0),
   [339] = DECL_SYS(sys_fanotify_mark, 0),
   [340] = DECL_SYS(sys_prlimit64, 0),
   [341] = DECL_SYS(sys_name_to_handle_at, 0),
   [342] = DECL_SYS(sys_open_by_handle_at, 0),
   [343] = DECL_SYS(sys_clock_adjtime32, 0),
   [344] = DECL_SYS(sys_syncfs, 0),
   [345] = DECL_SYS(sys_sendmmsg, 0),
   [346] = DECL_SYS(sys_setns, 0),
   [347] = DECL_SYS(sys_process_vm_readv, 0),
   [348] = DECL_SYS(sys_process_vm_writev, 0),
   [349] = DECL_SYS(sys_kcmp, 0),
   [350] = DECL_SYS(sys_finit_module, 0),
   [351] = DECL_SYS(sys_sched_setattr, 0),
   [352] = DECL_SYS(sys_sched_getattr, 0),
   [353] = DECL_SYS(sys_renameat2, 0),
   [354] = DECL_SYS(sys_seccomp, 0),
   [355] = DECL_SYS(sys_getrandom, 0),
   [356] = DECL_SYS(sys_memfd_create, 0),
   [357] = DECL_SYS(sys_bpf, 0),
   [358] = DECL_SYS(sys_execveat, 0),
   [359] = DECL_SYS(sys_socket, 0),
   [360] = DECL_SYS(sys_socketpair, 0),
   [361] = DECL_SYS(sys_bind, 0),
   [362] = DECL_SYS(sys_connect, 0),
   [363] = DECL_SYS(sys_listen, 0),
   [364] = DECL_SYS(sys_accept4, 0),
   [365] = DECL_SYS(sys_getsockopt, 0),
   [366] = DECL_SYS(sys_setsockopt, 0),
   [367] = DECL_SYS(sys_getsockname, 0),
   [368] = DECL_SYS(sys_getpeername, 0),
   [369] = DECL_SYS(sys_sendto, 0),
   [370] = DECL_SYS(sys_sendmsg, 0),
   [371] = DECL_SYS(sys_recvfrom, 0),
   [372] = DECL_SYS(sys_recvmsg, 0),
   [373] = DECL_SYS(sys_shutdown, 0),
   [374] = DECL_SYS(sys_userfaultfd, 0),
   [375] = DECL_SYS(sys_membarrier, 0),
   [376] = DECL_SYS(sys_mlock2, 0),
   [377] = DECL_SYS(sys_copy_file_range, 0),
   [378] = DECL_SYS(sys_preadv2, 0),
   [379] = DECL_SYS(sys_pwritev2, 0),
   [380] = DECL_SYS(sys_pkey_mprotect, 0),
   [381] = DECL_SYS(sys_pkey_alloc, 0),
   [382] = DECL_SYS(sys_pkey_free, 0),
   [383] = DECL_SYS(sys_statx, 0),
   [384] = DECL_SYS(sys_arch_prctl, 0),
   [385] = DECL_SYS(sys_io_pgetevents_time32, 0),
   [386] = DECL_SYS(sys_rseq, 0),
   [393] = DECL_SYS(sys_semget, 0),
   [394] = DECL_SYS(sys_semctl, 0),
   [395] = DECL_SYS(sys_shmget, 0),
   [396] = DECL_SYS(sys_shmctl, 0),
   [397] = DECL_SYS(sys_shmat, 0),
   [398] = DECL_SYS(sys_shmdt, 0),
   [399] = DECL_SYS(sys_msgget, 0),
   [400] = DECL_SYS(sys_msgsnd, 0),
   [401] = DECL_SYS(sys_msgrcv, 0),
   [402] = DECL_SYS(sys_msgctl, 0),
   [403] = DECL_SYS(sys_clock_gettime, 0),
   [404] = DECL_SYS(sys_clock_settime, 0),
   [405] = DECL_SYS(sys_clock_adjtime, 0),
   [406] = DECL_SYS(sys_clock_getres, 0),
   [407] = DECL_SYS(sys_clock_nanosleep, 0),
   [408] = DECL_SYS(sys_timer_gettime, 0),
   [409] = DECL_SYS(sys_timer_settime, 0),
   [410] = DECL_SYS(sys_timerfd_gettime, 0),
   [411] = DECL_SYS(sys_timerfd_settime, 0),
   [412] = DECL_SYS(sys_utimensat, 0),
   [413] = DECL_SYS(sys_pselect6, 0),
   [414] = DECL_SYS(sys_ppoll, 0),
   [416] = DECL_SYS(sys_io_pgetevents, 0),
   [417] = DECL_SYS(sys_recvmmsg, 0),
   [418] = DECL_SYS(sys_mq_timedsend, 0),
   [419] = DECL_SYS(sys_mq_timedreceive, 0),
   [420] = DECL_SYS(sys_semtimedop, 0),
   [421] = DECL_SYS(sys_rt_sigtimedwait, 0),
   [422] = DECL_SYS(sys_futex, 0),
   [423] = DECL_SYS(sys_sched_rr_get_interval, 0),
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
   [443] = DECL_SYS(sys_quotactl_fd, 0),
   [444] = DECL_SYS(sys_landlock_create_ruleset, 0),
   [445] = DECL_SYS(sys_landlock_add_rule, 0),
   [446] = DECL_SYS(sys_landlock_restrict_self, 0),
   [447] = DECL_SYS(sys_memfd_secret, 0),
   [448] = DECL_SYS(sys_process_mrelease, 0),

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

static void do_special_syscall(regs_t *r)
{
   struct task *curr = get_curr_task();
   const u32 sn = r->eax;
   const u32 fl = syscalls[sn].flags;
   const syscall_type fptr = syscalls[sn].fptr;
   const bool signals = ~fl & SYSFL_NO_SIG;
   const bool preemptable = ~fl & SYSFL_NO_PREEMPT;
   const bool traceable = ~fl & SYSFL_NO_TRACE;

   if (signals)
      process_signals(curr, sig_pre_syscall, r);

   if (preemptable)
      enable_preemption();

   if (traceable)
      trace_sys_enter(sn,r->ebx,r->ecx,r->edx,r->esi,r->edi,r->ebp);

   r->eax = (u32) fptr(r->ebx,r->ecx,r->edx,r->esi,r->edi,r->ebp);

   if (traceable)
      trace_sys_exit(sn,r->eax,r->ebx,r->ecx,r->edx,r->esi,r->edi,r->ebp);

   if (preemptable)
      disable_preemption();

   if (signals)
      process_signals(curr, sig_in_syscall, r);
}

static void do_syscall(regs_t *r)
{
   struct task *curr = get_curr_task();
   const u32 sn = r->eax;
   const syscall_type fptr = syscalls[sn].fptr;

   process_signals(curr, sig_pre_syscall, r);
   enable_preemption();
   {
      trace_sys_enter(sn,r->ebx,r->ecx,r->edx,r->esi,r->edi,r->ebp);
      r->eax = (u32) fptr(r->ebx,r->ecx,r->edx,r->esi,r->edi,r->ebp);
      trace_sys_exit(sn,r->eax,r->ebx,r->ecx,r->edx,r->esi,r->edi,r->ebp);
   }
   disable_preemption();
   process_signals(curr, sig_in_syscall, r);
}

void handle_syscall(regs_t *r)
{
   const u32 sn = r->eax;

   /*
    * In case of a sysenter syscall, the eflags are saved in kernel mode after
    * the cpu disabled the interrupts. Therefore, with the statement below we
    * force the IF flag to be set in any case (for the int 0x80 case it is not
    * necessary).
    */
   r->eflags |= EFLAGS_IF;

   save_current_task_state(r);
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
   /* Set the entry for the int 0x80 syscall interface */
   idt_set_entry(SYSCALL_SOFT_INTERRUPT,
                 syscall_int80_entry,
                 X86_KERNEL_CODE_SEL,
                 IDT_FLAG_PRESENT | IDT_FLAG_INT_GATE | IDT_FLAG_DPL3);

   /* Setup the sysenter interface */
   wrmsr(MSR_IA32_SYSENTER_CS, X86_KERNEL_CODE_SEL);
   wrmsr(MSR_IA32_SYSENTER_EIP, (ulong) &sysenter_entry);
}

