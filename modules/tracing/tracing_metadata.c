/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/syscalls.h>
#include <tilck/mods/tracing.h>

#include "syscall_types.h"

static const struct syscall_info __tracing_metadata[] =
{
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
   SYSCALL_TYPE_5(SYS_kill, "pid", "sig"),
   SYSCALL_TYPE_5(SYS_tkill, "tid", "sig"),

#if defined(__i386__)
   SYSCALL_TYPE_6(SYS_chown16, "path", "owner", "group"),
   SYSCALL_TYPE_6(SYS_lchown16, "path", "owner", "group"),

   SYSCALL_TYPE_7(SYS_fchown16, "fd", "owner", "group"),
#endif

   SYSCALL_TYPE_6(SYS_chown, "path", "owner", "group"),
   SYSCALL_TYPE_6(SYS_lchown, "path", "owner", "group"),

   SYSCALL_TYPE_7(SYS_fchown, "fd", "owner", "group"),

   SYSCALL_RW(SYS_read, "fd", "buf", &ptype_buffer, sys_param_out, "count"),
   SYSCALL_RW(SYS_write, "fd", "buf", &ptype_buffer, sys_param_in, "count"),
   SYSCALL_RW(SYS_readv, "fd", "iov", &ptype_iov_out, sys_param_out, "iovcnt"),
   SYSCALL_RW(SYS_writev, "fd", "iov", &ptype_iov_in, sys_param_in, "iovcnt"),

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

   { .sys_n = INVALID_SYSCALL },
};

const struct syscall_info *tracing_metadata = __tracing_metadata;
