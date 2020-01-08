/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#define SIMPLE_PARAM(_name, _type, _kind)                \
   {                                                     \
      .name = _name,                                     \
      .type = _type,                                     \
      .kind = _kind,                                     \
   }

#define BUFFER_PARAM(_name, _type, _kind, _sz_param)                 \
   {                                                                 \
      .name = _name,                                                 \
      .type = _type,                                                 \
      .kind = _kind,                                                 \
      .helper_param_name = _sz_param,                                \
      .real_sz_in_ret = true,                                        \
   }

#define COMPLEX_PARAM(_name, _type, _kind, _helper)                  \
   {                                                                 \
      .name = _name,                                                 \
      .type = _type,                                                 \
      .kind = _kind,                                                 \
      .helper_param_name = _helper,                                  \
   }

#define HIDDEN_PARAM(_name, _type, _kind)                \
   {                                                     \
      .name = _name,                                     \
      .type = _type,                                     \
      .kind = _kind,                                     \
      .invisible = true,                                 \
   }

/* Syscall (void) */
#define SYSCALL_TYPE_0(sys)                                          \
   {                                                                 \
      .sys_n = sys,                                                  \
      .n_params = 0,                                                 \
      .exp_block = false,                                            \
      .ret_type = &ptype_errno_or_val,                               \
      .params = { }                                                  \
   }

/* Syscall (int) */
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

/* Syscall (path, oct_int) */
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

/* Syscall (path) */
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

/* Syscall (path, path) */
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

/* Syscall (int, int) */
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

/* Syscall (path, int, int) */
#define SYSCALL_TYPE_6(sys, par1, par2, par3)                        \
   {                                                                 \
      .sys_n = sys,                                                  \
      .n_params = 3,                                                 \
      .exp_block = false,                                            \
      .ret_type = &ptype_errno_or_val,                               \
      .params = {                                                    \
         SIMPLE_PARAM(par1, &ptype_path, sys_param_in),              \
         SIMPLE_PARAM(par2, &ptype_int, sys_param_in),               \
         SIMPLE_PARAM(par3, &ptype_int, sys_param_in),               \
      }                                                              \
   }

/* Syscall (int, int, int) */
#define SYSCALL_TYPE_7(sys, par1, par2, par3)                        \
   {                                                                 \
      .sys_n = sys,                                                  \
      .n_params = 3,                                                 \
      .exp_block = false,                                            \
      .ret_type = &ptype_errno_or_val,                               \
      .params = {                                                    \
         SIMPLE_PARAM(par1, &ptype_int, sys_param_in),               \
         SIMPLE_PARAM(par2, &ptype_int, sys_param_in),               \
         SIMPLE_PARAM(par3, &ptype_int, sys_param_in),               \
      }                                                              \
   }

/* Syscall (int, buffer_type, int) */
#define SYSCALL_RW(sys, par1, par2, par2_type, par2_kind, par3)            \
   {                                                                       \
      .sys_n = sys,                                                        \
      .n_params = 3,                                                       \
      .exp_block = true,                                                   \
      .ret_type = &ptype_errno_or_val,                                     \
      .params = {                                                          \
         SIMPLE_PARAM(par1, &ptype_int, sys_param_in_out),                 \
         BUFFER_PARAM(par2, par2_type, par2_kind, par3),                   \
         SIMPLE_PARAM(par3, &ptype_int, sys_param_in),                     \
      },                                                                   \
   }
