/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/syscalls.h>
#include <tilck/mods/tracing.h>

#include "syscall_types.h"

/*
 * Each entry below gates on `#ifdef SYS_<name>` where the syscall
 * isn't universal across Tilck's architectures: i386 has a long
 * legacy tail (fork, open, pipe, *stat, mmap2, llseek, the old
 * signal API, 16-bit uid/gid, time-32, socketcall, ...) that
 * riscv64's asm-generic ABI doesn't carry. Tilck's
 * <tilck/common/syscalls.h> also synthesizes a few SYS_* macros
 * that musl doesn't expose (e.g. SYS_gettimeofday on i386), so by
 * the time we reach this table the gates reflect Tilck's
 * effective syscall set, not just musl's.
 */
static const struct syscall_info __tracing_metadata[] =
{
#ifdef SYS_fork
   SYSCALL_TYPE_0(SYS_fork),
#endif
#ifdef SYS_pause
   SYSCALL_TYPE_0(SYS_pause),
#endif
   SYSCALL_TYPE_0(SYS_getuid),
   SYSCALL_TYPE_0(SYS_getgid),
   SYSCALL_TYPE_0(SYS_geteuid),
   SYSCALL_TYPE_0(SYS_getegid),

#ifdef SYS_getuid16
   SYSCALL_TYPE_0(SYS_getuid16),
   SYSCALL_TYPE_0(SYS_getgid16),
   SYSCALL_TYPE_0(SYS_geteuid16),
   SYSCALL_TYPE_0(SYS_getegid16),
#endif

   SYSCALL_TYPE_0(SYS_gettid),
   SYSCALL_TYPE_0(SYS_setsid),
   SYSCALL_TYPE_0(SYS_sync),
   SYSCALL_TYPE_0(SYS_getppid),
#ifdef SYS_getpgrp
   SYSCALL_TYPE_0(SYS_getpgrp),
#endif
   SYSCALL_TYPE_0(SYS_sched_yield),

   SYSCALL_TYPE_1(SYS_close, "fd"),
   SYSCALL_TYPE_1(SYS_dup, "dup"),
   SYSCALL_TYPE_1(SYS_getpgid, "pid"),
   SYSCALL_TYPE_1(SYS_getsid, "pid"),

#ifdef SYS_creat
   SYSCALL_TYPE_2(SYS_creat, "path", "mode"),
#endif
#ifdef SYS_chmod
   SYSCALL_TYPE_2(SYS_chmod, "path", "mode"),
#endif
#ifdef SYS_mkdir
   SYSCALL_TYPE_2(SYS_mkdir, "path", "mode"),
#endif
#ifdef SYS_access
   /* access: mode is the R_OK|W_OK|X_OK|F_OK bitmask, not an octal
    * file mode — render via ptype_access_mode. */
   {
      .sys_n = SYS_access,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path,         sys_param_in),
         SIMPLE_PARAM("mode", &ptype_access_mode,  sys_param_in),
      },
   },
#endif

#ifdef SYS_unlink
   SYSCALL_TYPE_3(SYS_unlink, "path"),
#endif
#ifdef SYS_rmdir
   SYSCALL_TYPE_3(SYS_rmdir, "path"),
#endif
   SYSCALL_TYPE_3(SYS_chdir, "path"),

#ifdef SYS_link
   SYSCALL_TYPE_4(SYS_link, "oldpath", "newpath"),
#endif
#ifdef SYS_symlink
   SYSCALL_TYPE_4(SYS_symlink, "target", "linkpath"),
#endif
#ifdef SYS_rename
   SYSCALL_TYPE_4(SYS_rename, "oldpath", "newpath"),
#endif

   SYSCALL_TYPE_5(SYS_setpgid, "pid", "pgid"),
#ifdef SYS_dup2
   SYSCALL_TYPE_5(SYS_dup2, "oldfd", "newfd"),
#endif

#ifdef SYS_chown16
   SYSCALL_TYPE_6(SYS_chown16, "path", "owner", "group"),
   SYSCALL_TYPE_6(SYS_lchown16, "path", "owner", "group"),

   SYSCALL_TYPE_7(SYS_fchown16, "fd", "owner", "group"),
#endif

#ifdef SYS_chown
   SYSCALL_TYPE_6(SYS_chown, "path", "owner", "group"),
   SYSCALL_TYPE_6(SYS_lchown, "path", "owner", "group"),
#endif

   SYSCALL_TYPE_7(SYS_fchown, "fd", "owner", "group"),

   SYSCALL_RW(SYS_read, "fd", "buf", &ptype_big_buf, sys_param_out, "count"),
   SYSCALL_RW(SYS_write, "fd", "buf", &ptype_big_buf, sys_param_in, "count"),
   SYSCALL_RW(SYS_readv, "fd", "iov", &ptype_iov_out, sys_param_out, "iovcnt"),
   SYSCALL_RW(SYS_writev, "fd", "iov", &ptype_iov_in, sys_param_in, "iovcnt"),

   {
      .sys_n = SYS_kill,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("pid", &ptype_int, sys_param_in),
         SIMPLE_PARAM("sig", &ptype_signum, sys_param_in),
      }
   },

   {
      .sys_n = SYS_tkill,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("tid", &ptype_int, sys_param_in),
         SIMPLE_PARAM("sig", &ptype_signum, sys_param_in),
      }
   },

   {
      .sys_n = SYS_exit,
      .n_params = 1,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("status", &ptype_int, sys_param_in),
      }
   },

   {
      .sys_n = SYS_exit_group,
      .n_params = 1,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("status", &ptype_int, sys_param_in),
      }
   },

#ifdef SYS_vfork
   {
      .sys_n = SYS_vfork,
      .n_params = 0,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = { }
   },
#endif

   {
      .sys_n = SYS_getcwd,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         BUFFER_PARAM("buf", &ptype_buffer, sys_param_out, "size"),
         SIMPLE_PARAM("size", &ptype_int, sys_param_in),
      },
   },

#ifdef SYS_open
   {
      .sys_n = SYS_open,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
         SIMPLE_PARAM("flags", &ptype_open_flags, sys_param_in),
         SIMPLE_PARAM("mode", &ptype_oct, sys_param_in),
      }
   },
#endif

#ifdef STAT_SYSCALL_N
   {
      .sys_n = STAT_SYSCALL_N,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
         SIMPLE_PARAM("statbuf", &ptype_voidp, sys_param_out),
      },
   },
#endif

#ifdef LSTAT_SYSCALL_N
   {
      .sys_n = LSTAT_SYSCALL_N,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
         SIMPLE_PARAM("statbuf", &ptype_voidp, sys_param_out),
      },
   },
#endif

   {
      .sys_n = FSTAT_SYSCALL_N,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd", &ptype_int, sys_param_in),
         SIMPLE_PARAM("statbuf", &ptype_voidp, sys_param_out),
      },
   },

   {
      .sys_n = SYS_execve,
      .n_params = 3,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("filename", &ptype_path, sys_param_in),
         SIMPLE_PARAM("argv", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("envp", &ptype_voidp, sys_param_in),
      },
   },

   {
      .sys_n = SYS_brk,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_voidp,
      .params = {
         SIMPLE_PARAM("vaddr", &ptype_voidp, sys_param_in),
      }
   },

#ifdef SYS_waitpid

   /* waitpid is i386-only legacy; the universal replacement is
    * SYS_wait4, traced separately below. */

   {
      .sys_n = SYS_waitpid,
      .n_params = 3,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("pid",     &ptype_int,           sys_param_in),
         SIMPLE_PARAM("wstatus", &ptype_wstatus,       sys_param_out),
         SIMPLE_PARAM("options", &ptype_wait_options,  sys_param_in),
      }
   },

#endif

   {
      .sys_n = SYS_wait4,
      .n_params = 4,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("pid",     &ptype_int,          sys_param_in),
         SIMPLE_PARAM("wstatus", &ptype_wstatus,      sys_param_out),
         SIMPLE_PARAM("options", &ptype_wait_options, sys_param_in),
         SIMPLE_PARAM("rusage",  &ptype_voidp,        sys_param_out),
      }
   },

   {
      .sys_n = SYS_umask,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_oct,
      .params = {
         SIMPLE_PARAM("mask", &ptype_oct, sys_param_in),
      },
   },

   {
      .sys_n = SYS_ioctl,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd",      &ptype_int,        sys_param_in),
         SIMPLE_PARAM("request", &ptype_ioctl_cmd,  sys_param_in),
         /* argp is context-dependent: its pointee struct (termios,
          * winsize, int, ...) is determined by `request`. The save
          * callback (ptype_argp.c) switches on the helper value to
          * copy the right bytes; userspace dump dispatches by
          * `request` too. */
         COMPLEX_PARAM("argp",   &ptype_ioctl_argp,
                       sys_param_in_out, "request"),
      },
   },

   {
      .sys_n = FCNTL_SYSCALL_N,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd",  &ptype_int,       sys_param_in),
         SIMPLE_PARAM("cmd", &ptype_fcntl_cmd, sys_param_in),
         /* arg semantics depend on cmd: an int for F_DUPFD /
          * F_SETFD / F_SETFL / F_DUPFD_CLOEXEC; unused for
          * F_GETFD / F_GETFL. Userspace dispatch by helper. */
         COMPLEX_PARAM("arg", &ptype_fcntl_arg, sys_param_in, "cmd"),
      },
   },

   {
      .sys_n = SYS_uname,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("buf", &ptype_voidp, sys_param_in),
      },
   },

   {
      .sys_n = SYS_rt_sigaction,
      .n_params = 4,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("signum", &ptype_int, sys_param_in),
         SIMPLE_PARAM("act", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("oldact", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("sigsetsize", &ptype_int, sys_param_in),
      },
   },

   {
      .sys_n = SYS_rt_sigprocmask,
      .n_params = 4,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("how",        &ptype_sigprocmask_how, sys_param_in),
         SIMPLE_PARAM("set",        &ptype_voidp,           sys_param_in),
         SIMPLE_PARAM("oldset",     &ptype_voidp,           sys_param_in),
         SIMPLE_PARAM("sigsetsize", &ptype_int,             sys_param_in),
      },
   },

#ifdef SYS_pipe
   {
      .sys_n = SYS_pipe,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("pipefd", &ptype_int32_pair, sys_param_out),
      }
   },
#endif

   {
      .sys_n = SYS_pipe2,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("pipefd", &ptype_int32_pair, sys_param_out),
         SIMPLE_PARAM("flags", &ptype_open_flags, sys_param_in),
      }
   },

#ifdef SYS_set_thread_area
   {
      .sys_n = SYS_set_thread_area,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("u_info", &ptype_voidp, sys_param_in),
      }
   },
#endif

   {
      .sys_n = SYS_prctl,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("option", &ptype_prctl_option, sys_param_in),
      }
   },

   {
      .sys_n = MMAP_SYSCALL_N,
      .n_params = 6,
      .exp_block = false,
      .ret_type = &ptype_errno_or_ptr,
      .params = {
         SIMPLE_PARAM("addr",     &ptype_voidp,      sys_param_in),
         SIMPLE_PARAM("len",      &ptype_int,        sys_param_in),
         SIMPLE_PARAM("prot",     &ptype_mmap_prot,  sys_param_in),
         SIMPLE_PARAM("flags",    &ptype_mmap_flags, sys_param_in),
         SIMPLE_PARAM("fd",       &ptype_int,        sys_param_in),
         SIMPLE_PARAM("pgoffset", &ptype_int,        sys_param_in),
      }
   },

   {
      .sys_n = SYS_set_tid_address,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_voidp,
      .params = {
         SIMPLE_PARAM("tidptr", &ptype_voidp, sys_param_in),
      },
   },

#ifdef SYS_llseek
   {
      .sys_n = SYS_llseek,
      .n_params = 5,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd", &ptype_int, sys_param_in),
         COMPLEX_PARAM("off", &ptype_doff64, sys_param_in, "off_low"),
         HIDDEN_PARAM("off_low", &ptype_int, sys_param_in),
         SIMPLE_PARAM("result", &ptype_u64_ptr, sys_param_out),
         SIMPLE_PARAM("whence", &ptype_whence, sys_param_in),
      },
   },
#endif

   {
      .sys_n = SYS_getrusage,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("who", &ptype_int, sys_param_in),
         SIMPLE_PARAM("usage", &ptype_voidp, sys_param_out),
      },
   },

   {
      .sys_n = TILCK_CMD_SYSCALL,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("cmd", &ptype_int, sys_param_in),
      },
   },

   /* ---------------- Layer 0a: file-system syscalls ----------------
    * Coverage additions for syscalls that Tilck implements but the
    * tracer didn't have metadata for. Args use existing ptypes — a
    * later layer (Layer 1) will swap in symbolic ptypes for the
    * flag/option args (e.g. dup3.flags, fchmodat.flags), and Layer
    * 3 will add struct-aware capture for fstatat64.statbuf etc. */

   /* sync family — single fd in, errno out */
   SYSCALL_TYPE_1(SYS_fsync,     "fd"),
   SYSCALL_TYPE_1(SYS_fdatasync, "fd"),
   SYSCALL_TYPE_1(SYS_syncfs,    "fd"),

   /* fd-mode pair (mode is octal) */
   {
      .sys_n = SYS_fchmod,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd",   &ptype_int, sys_param_in),
         SIMPLE_PARAM("mode", &ptype_oct, sys_param_in),
      },
   },

   /* dup variants */
   {
      .sys_n = SYS_dup3,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("oldfd", &ptype_int,         sys_param_in),
         SIMPLE_PARAM("newfd", &ptype_int,         sys_param_in),
         SIMPLE_PARAM("flags", &ptype_open_flags,  sys_param_in),
      },
   },

   /* lseek (32-bit offset; the 64-bit form is SYS_llseek above) */
   {
      .sys_n = SYS_lseek,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd",     &ptype_int,    sys_param_in),
         SIMPLE_PARAM("offset", &ptype_int,    sys_param_in),
         SIMPLE_PARAM("whence", &ptype_whence, sys_param_in),
      },
   },

   /* pread / pwrite — read/write at an explicit file offset */
   {
      .sys_n = SYS_pread64,
      .n_params = 4,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd", &ptype_int, sys_param_in_out),
         BUFFER_PARAM("buf", &ptype_big_buf, sys_param_out, "count"),
         SIMPLE_PARAM("count",  &ptype_int, sys_param_in),
         SIMPLE_PARAM("offset", &ptype_int, sys_param_in),
      },
   },

   {
      .sys_n = SYS_pwrite64,
      .n_params = 4,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd", &ptype_int, sys_param_in_out),
         BUFFER_PARAM("buf", &ptype_big_buf, sys_param_in, "count"),
         SIMPLE_PARAM("count",  &ptype_int, sys_param_in),
         SIMPLE_PARAM("offset", &ptype_int, sys_param_in),
      },
   },

   /* getdents64: read directory entries */
   {
      .sys_n = SYS_getdents64,
      .n_params = 3,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd",    &ptype_int,   sys_param_in),
         SIMPLE_PARAM("dirp",  &ptype_voidp, sys_param_out),
         SIMPLE_PARAM("count", &ptype_int,   sys_param_in),
      },
   },

   /* readlink / readlinkat — out buffer with size limit. riscv64
    * only has the *at variant. */
#ifdef SYS_readlink
   {
      .sys_n = SYS_readlink,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
         BUFFER_PARAM("buf",  &ptype_buffer, sys_param_out, "bufsiz"),
         SIMPLE_PARAM("bufsiz", &ptype_int, sys_param_in),
      },
   },
#endif

   {
      .sys_n = SYS_readlinkat,
      .n_params = 4,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("dirfd", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("path",  &ptype_path, sys_param_in),
         BUFFER_PARAM("buf",   &ptype_buffer, sys_param_out, "bufsiz"),
         SIMPLE_PARAM("bufsiz", &ptype_int, sys_param_in),
      },
   },

   /* mount / umount (the modern umount; the legacy oldumount is
    * stubbed in Tilck and isn't traced). umount on i386 is at slot
    * 52, takes (target, flags). */
   {
      .sys_n = SYS_umount2,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("target", &ptype_path,        sys_param_in),
         SIMPLE_PARAM("flags",  &ptype_mount_flags, sys_param_in),
      },
   },

   /* mount: source + target are paths (64-byte slots). fstype is
    * always short ("tmpfs", "ext4", ...) so it shares the smaller
    * 32-byte ptype_buffer slot. The two path slots fill fmt0's
    * d0/d1; fstype lands in d2; that leaves the d3 16-byte slot
    * unused (data is voidp, no save). */
   {
      .sys_n = SYS_mount,
      .n_params = 5,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("source",  &ptype_path,        sys_param_in),
         SIMPLE_PARAM("target",  &ptype_path,        sys_param_in),
         SIMPLE_PARAM("fstype",  &ptype_buffer,      sys_param_in),
         SIMPLE_PARAM("flags",   &ptype_mount_flags, sys_param_in),
         SIMPLE_PARAM("data",    &ptype_voidp,       sys_param_in),
      },
   },

   /* *at variants — the dirfd-relative versions of the path syscalls
    * that already have plain entries above. Tilck wires real
    * implementations for these. */
   {
      .sys_n = SYS_openat,
      .n_params = 4,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("dirfd", &ptype_int,        sys_param_in),
         SIMPLE_PARAM("path",  &ptype_path,       sys_param_in),
         SIMPLE_PARAM("flags", &ptype_open_flags, sys_param_in),
         SIMPLE_PARAM("mode",  &ptype_oct,        sys_param_in),
      },
   },

   {
      .sys_n = SYS_faccessat,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("dirfd", &ptype_int,         sys_param_in),
         SIMPLE_PARAM("path",  &ptype_path,        sys_param_in),
         SIMPLE_PARAM("mode",  &ptype_access_mode, sys_param_in),
      },
   },

   {
      .sys_n = SYS_mkdirat,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("dirfd", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("path",  &ptype_path, sys_param_in),
         SIMPLE_PARAM("mode",  &ptype_oct,  sys_param_in),
      },
   },

   {
      .sys_n = SYS_unlinkat,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("dirfd", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("path",  &ptype_path, sys_param_in),
         SIMPLE_PARAM("flags", &ptype_int,  sys_param_in),
      },
   },

   {
      .sys_n = SYS_linkat,
      .n_params = 5,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("olddirfd", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("oldpath",  &ptype_path, sys_param_in),
         SIMPLE_PARAM("newdirfd", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("newpath",  &ptype_path, sys_param_in),
         SIMPLE_PARAM("flags",    &ptype_int,  sys_param_in),
      },
   },

   {
      .sys_n = SYS_symlinkat,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("target",   &ptype_path, sys_param_in),
         SIMPLE_PARAM("newdirfd", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("linkpath", &ptype_path, sys_param_in),
      },
   },

   {
      .sys_n = SYS_renameat2,
      .n_params = 5,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("olddirfd", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("oldpath",  &ptype_path, sys_param_in),
         SIMPLE_PARAM("newdirfd", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("newpath",  &ptype_path, sys_param_in),
         SIMPLE_PARAM("flags",    &ptype_int,  sys_param_in),
      },
   },

   {
      .sys_n = SYS_fchmodat,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("dirfd", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("path",  &ptype_path, sys_param_in),
         SIMPLE_PARAM("mode",  &ptype_oct,  sys_param_in),
      },
   },

   {
      .sys_n = SYS_fchownat,
      .n_params = 5,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("dirfd", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("path",  &ptype_path, sys_param_in),
         SIMPLE_PARAM("owner", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("group", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("flags", &ptype_int,  sys_param_in),
      },
   },

   /* ---------------- Layer 0b: process / scheduling syscalls ----------
    * Coverage additions for process-control syscalls Tilck implements
    * but the tracer didn't have metadata for. */

   SYSCALL_TYPE_0(SYS_getpid),

   SYSCALL_TYPE_1(SYS_setuid, "uid"),
   SYSCALL_TYPE_1(SYS_setgid, "gid"),

   /* `times()` returns clock_t and writes a struct tms (utime,
    * stime, cutime, cstime) — one user-pointer arg, voidp until
    * Layer 3 adds struct ptypes. */
   {
      .sys_n = SYS_times,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("buf", &ptype_voidp, sys_param_out),
      },
   },

   /* clone(): the workhorse process/thread creator. The kernel's
    * impl in Tilck handles a small subset (no full namespaces,
    * etc.) but the userland call is real. Args: flags, child_stack,
    * ptid, tls, ctid. flags is a bitmask (CLONE_VM, CLONE_FS, ...);
    * Layer 1 will add ptype_clone_flags for symbolic rendering. */
   {
      .sys_n = SYS_clone,
      .n_params = 5,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("flags",       &ptype_clone_flags, sys_param_in),
         SIMPLE_PARAM("child_stack", &ptype_voidp,       sys_param_in),
         SIMPLE_PARAM("ptid",        &ptype_voidp,       sys_param_in),
         SIMPLE_PARAM("tls",         &ptype_voidp,       sys_param_in),
         SIMPLE_PARAM("ctid",        &ptype_voidp,       sys_param_in),
      },
   },

   /* ---------------- Layer 0c: memory-mgmt syscalls -------------------
    * Tilck implements only munmap and madvise as real memory-mgmt
    * calls — mprotect, mlock, mremap, msync, mincore etc. are all
    * stubs returning -ENOSYS, so they have nothing useful to
    * trace. brk and mmap_pgoff (the i386 mmap2 entry) were already
    * covered. */
   {
      .sys_n = SYS_munmap,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("addr",   &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("length", &ptype_int,   sys_param_in),
      },
   },

   /* madvise: advice is an enum (MADV_NORMAL / MADV_DONTNEED /
    * MADV_FREE / ...). Layer 1 will swap ptype_int for
    * ptype_madvise_advice for symbolic rendering. */
   {
      .sys_n = SYS_madvise,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("addr",   &ptype_voidp,           sys_param_in),
         SIMPLE_PARAM("length", &ptype_int,             sys_param_in),
         SIMPLE_PARAM("advice", &ptype_madvise_advice,  sys_param_in),
      },
   },

   /* ---------------- Layer 0d: signals + timers -----------------------
    * Legacy signal API + clock/timer/utime syscalls. The struct
    * args (struct sigaction, struct timespec, struct timeval, ...)
    * render as bare voidp until Layer 3 adds struct ptypes — for
    * now the goal is just getting the syscall NAMED in the trace. */

   /*
    * legacy signal API (the rt_* variants were already covered) —
    * SYS_signal / SYS_sigaction / SYS_sigprocmask are i386-only in
    * musl. On x86_64 / riscv64 they're not in <sys/syscall.h> at
    * all (replaced by SYS_rt_sigaction / SYS_rt_sigprocmask). Gate
    * each entry on its own SYS_* so the file builds on every arch
    * without naming any.
    */
#ifdef SYS_signal
   {
      .sys_n = SYS_signal,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_voidp,        /* returns prev handler */
      .params = {
         SIMPLE_PARAM("signum",  &ptype_signum, sys_param_in),
         SIMPLE_PARAM("handler", &ptype_voidp,  sys_param_in),
      },
   },
#endif

#ifdef SYS_sigaction
   {
      .sys_n = SYS_sigaction,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("signum", &ptype_signum, sys_param_in),
         SIMPLE_PARAM("act",    &ptype_voidp,  sys_param_in),
         SIMPLE_PARAM("oldact", &ptype_voidp,  sys_param_out),
      },
   },
#endif

#ifdef SYS_sigprocmask
   {
      .sys_n = SYS_sigprocmask,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("how",    &ptype_sigprocmask_how, sys_param_in),
         SIMPLE_PARAM("set",    &ptype_voidp,           sys_param_in),
         SIMPLE_PARAM("oldset", &ptype_voidp,           sys_param_out),
      },
   },
#endif

   {
      .sys_n = SYS_rt_sigpending,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("set",        &ptype_voidp, sys_param_out),
         SIMPLE_PARAM("sigsetsize", &ptype_int,   sys_param_in),
      },
   },

   {
      .sys_n = SYS_rt_sigsuspend,
      .n_params = 2,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("mask",       &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("sigsetsize", &ptype_int,   sys_param_in),
      },
   },

   /* tgkill: targeted thread-group kill (signum gets ptype_signum). */
   {
      .sys_n = SYS_tgkill,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("tgid",  &ptype_int,    sys_param_in),
         SIMPLE_PARAM("tid",   &ptype_int,    sys_param_in),
         SIMPLE_PARAM("signo", &ptype_signum, sys_param_in),
      },
   },

   /* time-getter / time-setter syscalls. The clock/timer ABI on
    * i386 has both 32-bit (legacy) and 64-bit (time64) slots; we
    * trace the legacy slots — that's what userland on i386 calls
    * by default. */
   {
      .sys_n = SYS_gettimeofday,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("tv", &ptype_voidp, sys_param_out),
         SIMPLE_PARAM("tz", &ptype_voidp, sys_param_out),
      },
   },

   {
      .sys_n = SYS_nanosleep,
      .n_params = 2,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("req", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("rem", &ptype_voidp, sys_param_out),
      },
   },

   /*
    * Clock syscalls. On i386 the kernel implements both the legacy
    * (_time32) and modern (_time64) variants at separate slots.
    * musl exposes the suffixed names only on i386; x86_64 / riscv64
    * have just the plain SYS_clock_gettime / SYS_clock_getres. Gate
    * each entry on its own SYS_*.
    */
#ifdef SYS_clock_gettime32
   {
      .sys_n = SYS_clock_gettime32,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("clockid", &ptype_int,   sys_param_in),
         SIMPLE_PARAM("tp",      &ptype_voidp, sys_param_out),
      },
   },
#endif

#ifdef SYS_clock_gettime64
   {
      .sys_n = SYS_clock_gettime64,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("clockid", &ptype_int,   sys_param_in),
         SIMPLE_PARAM("tp",      &ptype_voidp, sys_param_out),
      },
   },
#endif

#ifdef SYS_clock_getres_time32
   {
      .sys_n = SYS_clock_getres_time32,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("clockid", &ptype_int,   sys_param_in),
         SIMPLE_PARAM("res",     &ptype_voidp, sys_param_out),
      },
   },
#endif

#ifdef SYS_clock_getres_time64
   {
      .sys_n = SYS_clock_getres_time64,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("clockid", &ptype_int,   sys_param_in),
         SIMPLE_PARAM("res",     &ptype_voidp, sys_param_out),
      },
   },
#endif

   /* Modern (suffix-free) clock_gettime / clock_getres — present on
    * x86_64 / riscv64, absent on i386 (which uses the *_time32 /
    * *_time64 split above). */
#ifdef SYS_clock_gettime
   {
      .sys_n = SYS_clock_gettime,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("clockid", &ptype_int,   sys_param_in),
         SIMPLE_PARAM("tp",      &ptype_voidp, sys_param_out),
      },
   },
#endif

#ifdef SYS_clock_getres
   {
      .sys_n = SYS_clock_getres,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("clockid", &ptype_int,   sys_param_in),
         SIMPLE_PARAM("res",     &ptype_voidp, sys_param_out),
      },
   },
#endif

   /* utime / utimes / utimensat / futimesat: file timestamp
    * setters. Pointer-to-times is voidp until Layer 3. The three
    * path-based variants are i386-only; riscv64's only filestamp
    * syscall is utimensat. */
#ifdef SYS_utime
   {
      .sys_n = SYS_utime,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path",  &ptype_path,  sys_param_in),
         SIMPLE_PARAM("times", &ptype_voidp, sys_param_in),
      },
   },
#endif

#ifdef SYS_utimes
   {
      .sys_n = SYS_utimes,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path,  sys_param_in),
         SIMPLE_PARAM("tvp",  &ptype_voidp, sys_param_in),
      },
   },
#endif

   {
      .sys_n = SYS_utimensat,
      .n_params = 4,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("dirfd", &ptype_int,   sys_param_in),
         SIMPLE_PARAM("path",  &ptype_path,  sys_param_in),
         SIMPLE_PARAM("times", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("flags", &ptype_int,   sys_param_in),
      },
   },

#ifdef SYS_futimesat
   {
      .sys_n = SYS_futimesat,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("dirfd", &ptype_int,   sys_param_in),
         SIMPLE_PARAM("path",  &ptype_path,  sys_param_in),
         SIMPLE_PARAM("tvp",   &ptype_voidp, sys_param_in),
      },
   },
#endif

   /* ---------------- Layer 0e: misc / poll / select / socket ---------- */

   /* poll / ppoll: pollfd array sits behind the fds pointer.
    * Capturing the array contents would need a struct-aware ptype;
    * for now the pointer renders as voidp. riscv64 only has ppoll. */
#ifdef SYS_poll
   {
      .sys_n = SYS_poll,
      .n_params = 3,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fds",     &ptype_voidp, sys_param_in_out),
         SIMPLE_PARAM("nfds",    &ptype_int,   sys_param_in),
         SIMPLE_PARAM("timeout", &ptype_int,   sys_param_in),
      },
   },
#endif

   {
      .sys_n = SYS_ppoll,
      .n_params = 5,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fds",        &ptype_voidp, sys_param_in_out),
         SIMPLE_PARAM("nfds",       &ptype_int,   sys_param_in),
         SIMPLE_PARAM("tmo_p",      &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("sigmask",    &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("sigsetsize", &ptype_int,   sys_param_in),
      },
   },

   /* select / pselect6: the fd_set bitmasks render as voidp until
    * a struct-aware fd_set ptype lands. riscv64 only has pselect6. */
#ifdef SYS_select
   {
      .sys_n = SYS_select,
      .n_params = 5,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("nfds",      &ptype_int,   sys_param_in),
         SIMPLE_PARAM("readfds",   &ptype_voidp, sys_param_in_out),
         SIMPLE_PARAM("writefds",  &ptype_voidp, sys_param_in_out),
         SIMPLE_PARAM("exceptfds", &ptype_voidp, sys_param_in_out),
         SIMPLE_PARAM("timeout",   &ptype_voidp, sys_param_in_out),
      },
   },
#endif

   {
      .sys_n = SYS_pselect6,
      .n_params = 6,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("nfds",      &ptype_int,   sys_param_in),
         SIMPLE_PARAM("readfds",   &ptype_voidp, sys_param_in_out),
         SIMPLE_PARAM("writefds",  &ptype_voidp, sys_param_in_out),
         SIMPLE_PARAM("exceptfds", &ptype_voidp, sys_param_in_out),
         SIMPLE_PARAM("timeout",   &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("sigmask",   &ptype_voidp, sys_param_in),
      },
   },

   /* reboot: 4-arg LINUX_REBOOT_CMD_* style. cmd is an enum
    * (HALT/POWER_OFF/RESTART/...) — Layer 1 will add a symbolic
    * ptype_reboot_cmd if it turns out to matter. */
   {
      .sys_n = SYS_reboot,
      .n_params = 4,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("magic1", &ptype_int,   sys_param_in),
         SIMPLE_PARAM("magic2", &ptype_int,   sys_param_in),
         SIMPLE_PARAM("cmd",    &ptype_int,   sys_param_in),
         SIMPLE_PARAM("arg",    &ptype_voidp, sys_param_in),
      },
   },

   /* socketcall: i386's BSD-socket multiplexer. `call` is the
    * sub-call number (SYS_SOCKET, SYS_BIND, SYS_CONNECT, ...);
    * `args` is a pointer to a vararg-style array of arg words.
    * Layer 1 could add ptype_socketcall_op for symbolic rendering
    * of the sub-call name. */
#ifdef SYS_socketcall
   {
      .sys_n = SYS_socketcall,
      .n_params = 2,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("call", &ptype_int,   sys_param_in),
         SIMPLE_PARAM("args", &ptype_voidp, sys_param_in),
      },
   },
#endif

   /* (Legacy 16-bit setuid/setgid handlers exist in the Tilck
    * kernel for ABI compat with very old binaries — slot 23/46 —
    * but glibc's SYS_setuid16/SYS_setgid16 macros aren't exposed
    * on i386, so there's no constant to reference here. Modern
    * libc programs hit slot 213/214 via SYS_setuid / SYS_setgid
    * above; that's what we trace.) */

   /* i386-only: 64-bit truncate / fstatat64. x86_64 / riscv64 have
    * these under different names (plain SYS_truncate, SYS_ftruncate,
    * SYS_newfstatat — covered separately below). */
#ifdef SYS_truncate64
   {
      .sys_n = SYS_truncate64,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path",   &ptype_path, sys_param_in),
         SIMPLE_PARAM("length", &ptype_int,  sys_param_in),
      },
   },
#endif

#ifdef SYS_ftruncate64
   {
      .sys_n = SYS_ftruncate64,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd",     &ptype_int, sys_param_in),
         SIMPLE_PARAM("length", &ptype_int, sys_param_in),
      },
   },
#endif

#ifdef SYS_fstatat64
   {
      .sys_n = SYS_fstatat64,
      .n_params = 4,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("dirfd",   &ptype_int,   sys_param_in),
         SIMPLE_PARAM("path",    &ptype_path,  sys_param_in),
         SIMPLE_PARAM("statbuf", &ptype_voidp, sys_param_out),
         SIMPLE_PARAM("flags",   &ptype_int,   sys_param_in),
      },
   },
#endif

   /* riscv64's path-based stat-family replacement. Same 4-arg
    * signature as i386's fstatat64; the name is the only diff. */
#ifdef SYS_newfstatat
   {
      .sys_n = SYS_newfstatat,
      .n_params = 4,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("dirfd",   &ptype_int,   sys_param_in),
         SIMPLE_PARAM("path",    &ptype_path,  sys_param_in),
         SIMPLE_PARAM("statbuf", &ptype_voidp, sys_param_out),
         SIMPLE_PARAM("flags",   &ptype_int,   sys_param_in),
      },
   },
#endif

   /* Suffix-free truncate / ftruncate — x86_64 / riscv64 have these
    * as plain 64-bit syscalls. */
#ifdef SYS_truncate
   {
      .sys_n = SYS_truncate,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path",   &ptype_path, sys_param_in),
         SIMPLE_PARAM("length", &ptype_int,  sys_param_in),
      },
   },
#endif

#ifdef SYS_ftruncate
   {
      .sys_n = SYS_ftruncate,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd",     &ptype_int, sys_param_in),
         SIMPLE_PARAM("length", &ptype_int, sys_param_in),
      },
   },
#endif

   { .sys_n = INVALID_SYSCALL },
};

const struct syscall_info *tracing_metadata = __tracing_metadata;

