/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/mods/tracing.h>

#include <sys/syscall.h> // system header

#if defined(__x86_64__)
   #define STAT_SYSCALL_N      SYS_stat
   #define LSTAT_SYSCALL_N     SYS_lstat
   #define FSTAT_SYSCALL_N     SYS_fstat
#elif defined(__i386__)
   #define STAT_SYSCALL_N      SYS_stat64
   #define LSTAT_SYSCALL_N     SYS_lstat64
   #define FSTAT_SYSCALL_N     SYS_fstat64
#else
   #error Architecture not supported
#endif


#define SIMPLE_PARAM(_name, _type, _kind)                \
   {                                                     \
      .name = _name,                                     \
      .type = _type,                                     \
      .kind = _kind,                                     \
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

static const struct syscall_info __tracing_metadata[] =
{
   SYSCALL_TYPE_0(SYS_fork),
   SYSCALL_TYPE_0(SYS_vfork),
   SYSCALL_TYPE_0(SYS_pause),
   SYSCALL_TYPE_0(SYS_getuid),
   SYSCALL_TYPE_0(SYS_getgid),
   SYSCALL_TYPE_0(SYS_geteuid),
   SYSCALL_TYPE_0(SYS_getegid),
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

   {
      .sys_n = SYS_read,
      .n_params = 3,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {

         SIMPLE_PARAM("fd", &ptype_int, sys_param_in),

         {
            .name = "buf",
            .type = &ptype_buffer,
            .kind = sys_param_out,
            .size_param_name = "count",
            .real_sz_in_ret = true,
         },

         SIMPLE_PARAM("count", &ptype_int, sys_param_in),
      },
   },

   {
      .sys_n = SYS_write,
      .n_params = 3,
      .exp_block = true,
      .ret_type = &ptype_errno_or_val,
      .params = {

         SIMPLE_PARAM("fd", &ptype_int, sys_param_in),

         {
            .name = "buf",
            .type = &ptype_buffer,
            .kind = sys_param_in,
            .size_param_name = "count",
            .real_sz_in_ret = true,
         },

         SIMPLE_PARAM("count", &ptype_int, sys_param_in),
      },
   },

   {
      .sys_n = SYS_getcwd,
      .n_params = 2,
      .exp_block = false,
      .ret_type = &ptype_errno_or_val,
      .params = {

         {
            .name = "buf",
            .type = &ptype_buffer,
            .kind = sys_param_out,
            .size_param_name = "size",
            .real_sz_in_ret = true,
         },

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
         SIMPLE_PARAM("flags", &ptype_voidp, sys_param_in),
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

   { .sys_n = INVALID_SYSCALL },
};

const struct syscall_info *tracing_metadata = __tracing_metadata;
