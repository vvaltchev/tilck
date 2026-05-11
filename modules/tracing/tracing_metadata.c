/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/syscalls.h>
#include <tilck/mods/tracing.h>

#include "syscall_types.h"

static const struct syscall_info __tracing_metadata[] =
{
#if defined(__i386__) || defined(__x86_64)
   SYSCALL_TYPE_0(SYS_fork),
   SYSCALL_TYPE_0(SYS_pause),
   SYSCALL_TYPE_0(SYS_getuid),
   SYSCALL_TYPE_0(SYS_getgid),
   SYSCALL_TYPE_0(SYS_geteuid),
   SYSCALL_TYPE_0(SYS_getegid),

#if defined(__i386__)
   SYSCALL_TYPE_0(SYS_getuid16),
   SYSCALL_TYPE_0(SYS_getgid16),
   SYSCALL_TYPE_0(SYS_geteuid16),
   SYSCALL_TYPE_0(SYS_getegid16),
#endif

   SYSCALL_TYPE_0(SYS_gettid),
   SYSCALL_TYPE_0(SYS_setsid),
   SYSCALL_TYPE_0(SYS_sync),
   SYSCALL_TYPE_0(SYS_getppid),
   SYSCALL_TYPE_0(SYS_getpgrp),
   SYSCALL_TYPE_0(SYS_sched_yield),

   SYSCALL_TYPE_1(SYS_close, "fd"),
   SYSCALL_TYPE_1(SYS_dup, "dup"),
   SYSCALL_TYPE_1(SYS_getpgid, "pid"),
   SYSCALL_TYPE_1(SYS_getsid, "pid"),

   SYSCALL_TYPE_2(SYS_creat, "path", "mode"),
   SYSCALL_TYPE_2(SYS_chmod, "path", "mode"),
   SYSCALL_TYPE_2(SYS_mkdir, "path", "mode"),
   SYSCALL_TYPE_2(SYS_access, "path", "mode"),

   SYSCALL_TYPE_3(SYS_unlink, "path"),
   SYSCALL_TYPE_3(SYS_rmdir, "path"),
   SYSCALL_TYPE_3(SYS_chdir, "path"),

   SYSCALL_TYPE_4(SYS_link, "oldpath", "newpath"),
   SYSCALL_TYPE_4(SYS_symlink, "target", "linkpath"),
   SYSCALL_TYPE_4(SYS_rename, "oldpath", "newpath"),

   SYSCALL_TYPE_5(SYS_setpgid, "pid", "pgid"),
   SYSCALL_TYPE_5(SYS_dup2, "oldfd", "newfd"),

#if defined(__i386__)
   SYSCALL_TYPE_6(SYS_chown16, "path", "owner", "group"),
   SYSCALL_TYPE_6(SYS_lchown16, "path", "owner", "group"),

   SYSCALL_TYPE_7(SYS_fchown16, "fd", "owner", "group"),
#endif

   SYSCALL_TYPE_6(SYS_chown, "path", "owner", "group"),
   SYSCALL_TYPE_6(SYS_lchown, "path", "owner", "group"),

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

   {
      .sys_n = SYS_vfork,
      .n_params = 0,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = { }
   },

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

#ifdef __i386__

   /* waitpid is old and has been supported on amd64. Replacement: wait4 */

   {
      .sys_n = SYS_waitpid,
      .n_params = 3,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("pid", &ptype_int, sys_param_in),
         SIMPLE_PARAM("wstatus", &ptype_voidp, sys_param_out),
         SIMPLE_PARAM("options", &ptype_int, sys_param_in),
      }
   },

#endif

   {
      .sys_n = SYS_wait4,
      .n_params = 4,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("pid", &ptype_int, sys_param_in),
         SIMPLE_PARAM("wstatus", &ptype_voidp, sys_param_out),
         SIMPLE_PARAM("options", &ptype_int, sys_param_in),
         SIMPLE_PARAM("rusage", &ptype_voidp, sys_param_out),
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
         SIMPLE_PARAM("fd", &ptype_int, sys_param_in),
         SIMPLE_PARAM("request", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("argp", &ptype_voidp, sys_param_in),
      },
   },

   {
      .sys_n = FCNTL_SYSCALL_N,
      .n_params = 3,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd", &ptype_int, sys_param_in),
         SIMPLE_PARAM("cmd", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("arg", &ptype_voidp, sys_param_in),
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
         SIMPLE_PARAM("how", &ptype_int, sys_param_in),
         SIMPLE_PARAM("set", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("oldset", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("sigsetsize", &ptype_int, sys_param_in),
      },
   },

   {
      .sys_n = SYS_pipe,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("pipefd", &ptype_int32_pair, sys_param_out),
      }
   },

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

   {
      .sys_n = SYS_set_thread_area,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("u_info", &ptype_voidp, sys_param_in),
      }
   },

   {
      .sys_n = SYS_prctl,
      .n_params = 1,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("option", &ptype_int, sys_param_in),
      }
   },

   {
      .sys_n = MMAP_SYSCALL_N,
      .n_params = 6,
      .exp_block = false,
      .ret_type = &ptype_errno_or_ptr,
      .params = {
         SIMPLE_PARAM("addr", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("len", &ptype_int, sys_param_in),
         SIMPLE_PARAM("prot", &ptype_int, sys_param_in),
         SIMPLE_PARAM("flags", &ptype_int, sys_param_in),
         SIMPLE_PARAM("fd", &ptype_int, sys_param_in),
         SIMPLE_PARAM("pgoffset", &ptype_int, sys_param_in),
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

#ifdef __i386__
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

   /* readlink / readlinkat — out buffer with size limit */
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
         SIMPLE_PARAM("target", &ptype_path, sys_param_in),
         SIMPLE_PARAM("flags",  &ptype_int,  sys_param_in),
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
         SIMPLE_PARAM("source",  &ptype_path,   sys_param_in),
         SIMPLE_PARAM("target",  &ptype_path,   sys_param_in),
         SIMPLE_PARAM("fstype",  &ptype_buffer, sys_param_in),
         SIMPLE_PARAM("flags",   &ptype_int,    sys_param_in),
         SIMPLE_PARAM("data",    &ptype_voidp,  sys_param_in),
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
         SIMPLE_PARAM("dirfd", &ptype_int,  sys_param_in),
         SIMPLE_PARAM("path",  &ptype_path, sys_param_in),
         SIMPLE_PARAM("mode",  &ptype_int,  sys_param_in),
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

#ifdef __i386__
   /* i386-only: 64-bit truncate / fstatat64 (x86_64 has them under
    * different names). */
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

#else

/*
 * TODO: add tracing metadata for AARCH64
 */

#endif

   { .sys_n = INVALID_SYSCALL },
};

const struct syscall_info *tracing_metadata = __tracing_metadata;

