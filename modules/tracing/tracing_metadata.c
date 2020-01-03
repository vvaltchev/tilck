/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/mods/tracing.h>

#include <sys/syscall.h> // system header

#if defined(__x86_64__)

   #define STAT_SYSCALL_N      SYS_stat
   #define LSTAT_SYSCALL_N     SYS_lstat
   #define FSTAT_SYSCALL_N     SYS_fstat
   #define FCNTL_SYSCALL_N     SYS_fcntl

#elif defined(__i386__)

   #define STAT_SYSCALL_N      SYS_stat64
   #define LSTAT_SYSCALL_N     SYS_lstat64
   #define FSTAT_SYSCALL_N     SYS_fstat64
   #define FCNTL_SYSCALL_N     SYS_fcntl64

   #undef SYS_getuid
   #undef SYS_getgid
   #undef SYS_geteuid
   #undef SYS_getegid

   #define SYS_getuid            199
   #define SYS_getgid            200
   #define SYS_geteuid           201
   #define SYS_getegid           202

   #define SYS_getuid16          24
   #define SYS_getgid16          47
   #define SYS_geteuid16         49
   #define SYS_getegid16         50

#else
   #error Architecture not supported
#endif


#define SIMPLE_PARAM(_name, _type, _kind)                \
   {                                                     \
      .name = _name,                                     \
      .type = _type,                                     \
      .kind = _kind,                                     \
   }

#define BUFFER_PARAM(_name, _type, _kind, _sz_param)                    \
   {                                                                 \
      .name = _name,                                                 \
      .type = _type,                                                 \
      .kind = _kind,                                                 \
      .size_param_name = _sz_param,                                  \
      .real_sz_in_ret = true,                                        \
   }

#define SYSCALL_TYPE_0(sys)                                          \
   {                                                                 \
      .sys_n = sys,                                                  \
      .n_params = 0,                                                 \
      .exp_block = false,                                            \
      .ret_type = &ptype_errno_or_val,                               \
      .params = { }                                                  \
   }

#define SYSCALL_TYPE_1(sys, par1)                                    \
   {                                                                 \
      .sys_n = sys,                                                  \
      .n_params = 1,                                                 \
      .exp_block = false,                                            \
      .ret_type = &ptype_errno_or_val,                               \
      .params = {                                                    \
         SIMPLE_PARAM(par1, &ptype_int, sys_param_in),               \
      }                                                              \
   }

#define SYSCALL_TYPE_2(sys, par1, par2)                              \
   {                                                                 \
      .sys_n = sys,                                                  \
      .n_params = 2,                                                 \
      .exp_block = false,                                            \
      .ret_type = &ptype_errno_or_val,                               \
      .params = {                                                    \
         SIMPLE_PARAM(par1, &ptype_path, sys_param_in),              \
         SIMPLE_PARAM(par2, &ptype_oct, sys_param_in),               \
      }                                                              \
   }

#define SYSCALL_TYPE_3(sys, par1)                                    \
   {                                                                 \
      .sys_n = sys,                                                  \
      .n_params = 1,                                                 \
      .exp_block = false,                                            \
      .ret_type = &ptype_errno_or_val,                               \
      .params = {                                                    \
         SIMPLE_PARAM(par1, &ptype_path, sys_param_in),              \
      }                                                              \
   }

#define SYSCALL_TYPE_4(sys, par1, par2)                              \
   {                                                                 \
      .sys_n = sys,                                                  \
      .n_params = 2,                                                 \
      .exp_block = false,                                            \
      .ret_type = &ptype_errno_or_val,                               \
      .params = {                                                    \
         SIMPLE_PARAM(par1, &ptype_path, sys_param_in),              \
         SIMPLE_PARAM(par2, &ptype_path, sys_param_in),              \
      }                                                              \
   }

#define SYSCALL_TYPE_5(sys, par1, par2)                              \
   {                                                                 \
      .sys_n = sys,                                                  \
      .n_params = 2,                                                 \
      .exp_block = false,                                            \
      .ret_type = &ptype_errno_or_val,                               \
      .params = {                                                    \
         SIMPLE_PARAM(par1, &ptype_int, sys_param_in),               \
         SIMPLE_PARAM(par2, &ptype_int, sys_param_in),               \
      }                                                              \
   }

#define SYSCALL_RW(sys, par1, par2, par2_type, par2_kind, par3)            \
   {                                                                       \
      .sys_n = sys,                                                        \
      .n_params = 3,                                                       \
      .exp_block = true,                                                   \
      .ret_type = &ptype_errno_or_val,                                     \
      .params = {                                                          \
         SIMPLE_PARAM(par1, &ptype_int, sys_param_in),                     \
         BUFFER_PARAM(par2, par2_type, par2_kind, par3),                   \
         SIMPLE_PARAM(par3, &ptype_int, sys_param_in),                     \
      },                                                                   \
   }

static const struct syscall_info __tracing_metadata[] =
{
   SYSCALL_TYPE_0(SYS_fork),
   SYSCALL_TYPE_0(SYS_vfork),
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
   SYSCALL_TYPE_1(SYS_exit, "status"),
   SYSCALL_TYPE_1(SYS_exit_group, "status"),

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

   SYSCALL_RW(SYS_read, "fd", "buf", &ptype_buffer, sys_param_out, "count"),
   SYSCALL_RW(SYS_write, "fd", "buf", &ptype_buffer, sys_param_in, "count"),
   SYSCALL_RW(SYS_readv, "fd", "iov", &ptype_iov_out, sys_param_out, "iovcnt"),
   SYSCALL_RW(SYS_writev, "fd", "iov", &ptype_iov_in, sys_param_in, "iovcnt"),

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
      .exp_block = false,
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

   { .sys_n = INVALID_SYSCALL },
};

const struct syscall_info *tracing_metadata = __tracing_metadata;
