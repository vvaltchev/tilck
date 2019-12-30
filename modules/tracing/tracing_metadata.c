/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/mods/tracing.h>

#include <sys/syscall.h> // system header

#define SIMPLE_PARAM(_name, _type, _kind)                \
   {                                                     \
      .name = _name,                                     \
      .type = _type,                                     \
      .kind = _kind,                                     \
   }

static const struct syscall_info __tracing_metadata[] =
{
   {
      .sys_n = SYS_read,
      .n_params = 3,
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
      .sys_n = SYS_open,
      .n_params = 3,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
         SIMPLE_PARAM("flags", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("mode", &ptype_oct, sys_param_in),
      }
   },

   {
      .sys_n = SYS_stat64,
      .n_params = 2,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
         SIMPLE_PARAM("statbuf", &ptype_voidp, sys_param_out),
      },
   },

   {
      .sys_n = SYS_lstat64,
      .n_params = 2,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
         SIMPLE_PARAM("statbuf", &ptype_voidp, sys_param_out),
      },
   },

   {
      .sys_n = SYS_fstat64,
      .n_params = 2,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd", &ptype_int, sys_param_in),
         SIMPLE_PARAM("statbuf", &ptype_voidp, sys_param_out),
      },
   },

   {
      .sys_n = SYS_execve,
      .n_params = 3,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("filename", &ptype_path, sys_param_in),
         SIMPLE_PARAM("argv", &ptype_voidp, sys_param_in),
         SIMPLE_PARAM("envp", &ptype_voidp, sys_param_in),
      },
   },

   {
      .sys_n = SYS_close,
      .n_params = 1,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd", &ptype_int, sys_param_in),
      }
   },

   {
      .sys_n = SYS_dup,
      .n_params = 1,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("fd", &ptype_int, sys_param_in),
      }
   },

   {
      .sys_n = SYS_dup2,
      .n_params = 2,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("oldfd", &ptype_int, sys_param_in),
         SIMPLE_PARAM("newfd", &ptype_int, sys_param_in),
      }
   },

   {
      .sys_n = SYS_brk,
      .n_params = 1,
      .ret_type = &ptype_voidp,
      .params = {
         SIMPLE_PARAM("vaddr", &ptype_voidp, sys_param_in),
      }
   },

   {
      .sys_n = SYS_waitpid,
      .n_params = 3,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("pid", &ptype_int, sys_param_in),
         SIMPLE_PARAM("wstatus", &ptype_voidp, sys_param_out),
         SIMPLE_PARAM("options", &ptype_int, sys_param_in),
      }
   },

   {
      .sys_n = SYS_wait4,
      .n_params = 4,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("pid", &ptype_int, sys_param_in),
         SIMPLE_PARAM("wstatus", &ptype_voidp, sys_param_out),
         SIMPLE_PARAM("options", &ptype_int, sys_param_in),
         SIMPLE_PARAM("rusage", &ptype_voidp, sys_param_out),
      }
   },

   {
      .sys_n = SYS_creat,
      .n_params = 2,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
         SIMPLE_PARAM("mode", &ptype_oct, sys_param_in),
      }
   },

   {
      .sys_n = SYS_link,
      .n_params = 2,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("oldpath", &ptype_path, sys_param_in),
         SIMPLE_PARAM("newpath", &ptype_path, sys_param_in),
      }
   },

   {
      .sys_n = SYS_symlink,
      .n_params = 2,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("target", &ptype_path, sys_param_in),
         SIMPLE_PARAM("linkpath", &ptype_path, sys_param_in),
      }
   },

   {
      .sys_n = SYS_rename,
      .n_params = 2,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("oldpath", &ptype_path, sys_param_in),
         SIMPLE_PARAM("newpath", &ptype_path, sys_param_in),
      }
   },

   {
      .sys_n = SYS_unlink,
      .n_params = 1,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
      }
   },

   {
      .sys_n = SYS_rmdir,
      .n_params = 1,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
      }
   },

   {
      .sys_n = SYS_chdir,
      .n_params = 1,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
      }
   },

   {
      .sys_n = SYS_chmod,
      .n_params = 2,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
         SIMPLE_PARAM("mode", &ptype_oct, sys_param_in),
      }
   },

   {
      .sys_n = SYS_mkdir,
      .n_params = 2,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("path", &ptype_path, sys_param_in),
         SIMPLE_PARAM("mode", &ptype_oct, sys_param_in),
      }
   },

   {
      .sys_n = SYS_kill,
      .n_params = 2,
      .ret_type = &ptype_errno_or_val,
      .params = {
         SIMPLE_PARAM("pid", &ptype_int, sys_param_in),
         SIMPLE_PARAM("sig", &ptype_int, sys_param_in),
      }
   },

   { .sys_n = INVALID_SYSCALL },
};

const struct syscall_info *tracing_metadata = __tracing_metadata;
