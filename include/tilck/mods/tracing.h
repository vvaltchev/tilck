/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/syscalls.h>

#define INVALID_SYSCALL           ((u32) -1)
#define NO_SLOT                           -1
#define TRACED_SYSCALLS_STR_LEN         512u

extern bool *traced_syscalls;

enum trace_event_type {
   te_invalid,
   te_sys_enter,
   te_sys_exit,
};

struct trace_event {

   enum trace_event_type type;
   int tid;

   u64 sys_time;

   u32 sys;
   sptr retval;
   uptr args[6];

   union {

      struct {
         char d0[64];
         char d1[64];
         char d2[32];
         char d3[16];
      } fmt0;

      struct {
         char d0[128];
         char d1[32];
         char d2[16];
      } fmt1;
   };
};

STATIC_ASSERT(sizeof(struct trace_event) <= 256);

struct sys_param_type {

   const char *name;
   u32 slot_size;

   /* Returns false if buf_size is too small */
   bool (*save)(void *ptr, sptr size, char *buf, size_t buf_size);

   /* Returns false if dest_buf_size is too small */
   bool (*dump_from_data)(char *buf, sptr bs, sptr rsz, char *dst, size_t d_bs);

   /* Returns false if dest_buf_size is too small */
   bool (*dump_from_val)(uptr val, char *dest, size_t dest_buf_size);
};

enum sys_param_kind {
   sys_param_in,
   sys_param_out,
   sys_param_in_out,
};

struct sys_param_info {

   const char *name;

   const struct sys_param_type *type;

   /* IN, OUT or IN/OUT */
   enum sys_param_kind kind;

   /* name of another param in the same func used as size of this buffer */
   const char *size_param_name;

   /* true if the retval of the func represents the real value of this buffer */
   bool real_sz_in_ret;
};

enum sys_saved_param_fmt {
   sys_fmt0 = 0,
   sys_fmt1 = 1,
};

struct syscall_info {

   /* syscall number */
   u32 sys_n;

   /* number of parameters */
   int n_params;

   /* return type of the syscall */
   const struct sys_param_type *ret_type;

   /* info about its parameters */
   struct sys_param_info params[6];
};

void
init_tracing(void);

bool
read_trace_event(struct trace_event *e, u32 timeout_ticks);

void
trace_syscall_enter(u32 sys,
                    uptr a1, uptr a2, uptr a3, uptr a4, uptr a5, uptr a6);
void
trace_syscall_exit(u32 sys, sptr retval,
                   uptr a1, uptr a2, uptr a3, uptr a4, uptr a5, uptr a6);

const char *
tracing_get_syscall_name(u32 n);

const struct syscall_info *
tracing_get_syscall_info(u32 n);

bool
tracing_get_slot(struct trace_event *e,
                 const struct syscall_info *si,
                 int p_idx,
                 char **buf,
                 size_t *s);

int
tracing_get_param_idx(const struct syscall_info *si, const char *name);

const char *
get_errno_name(int errno);

int
get_traced_syscalls_count(void);

void
get_traced_syscalls_str(char *buf, size_t len);

int
set_traced_syscalls(const char *str);

static ALWAYS_INLINE bool
tracing_is_enabled_on_sys(u32 sys_n)
{
   if (sys_n >= MAX_SYSCALLS)
      return false;

   return traced_syscalls[sys_n];
}

extern const struct syscall_info *tracing_metadata;

extern const struct sys_param_type ptype_int;
extern const struct sys_param_type ptype_voidp;
extern const struct sys_param_type ptype_oct;
extern const struct sys_param_type ptype_errno_or_val;
extern const struct sys_param_type ptype_buffer;
extern const struct sys_param_type ptype_path;
