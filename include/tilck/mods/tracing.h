/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck_gen_headers/mod_tracing.h>
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/syscalls.h>

#define INVALID_SYSCALL           ((u32) -1)
#define NO_SLOT                           -1
#define TRACED_SYSCALLS_STR_LEN         128u
#define TRACE_PRINTK_TRUNC_STR       "{...}"

enum trace_event_type {
   te_invalid,
   te_sys_enter,
   te_sys_exit,
   te_printk,
   te_signal_delivered,
   te_killed,
};

struct syscall_event_data {

   u32 sys;
   long retval;
   ulong args[6];

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

struct printk_event_data {
   short level;      /* Verbosity level. Must be > 1 */
   bool in_irq;
   bool __unused_0;
   char buf[192];    /* Actual log entry */
};

struct signal_event_data {
   int signum;
};

struct trace_event {

   enum trace_event_type type;
   int tid;

   u64 sys_time;

   union {
      struct syscall_event_data sys_ev;
      struct printk_event_data p_ev;
      struct signal_event_data sig_ev;
   };
};

STATIC_ASSERT(sizeof(struct trace_event) <= 256);

enum sys_param_ui_type {

   ui_type_other,
   ui_type_integer,
   ui_type_string,
};

struct sys_param_type {

   const char *name;
   u32 slot_size;

   enum sys_param_ui_type ui_type;

   /* Returns false if buf_size is too small */
   bool (*save)(void *ptr, long size, char *buf, size_t buf_size);

   /* Returns false if dest_buf_size is too small */
   bool (*dump)(ulong orig, char *b, long bs, long hlp, char *dst, size_t d_bs);

   /* Returns false if dest_buf_size is too small */
   bool (*dump_from_val)(ulong val, long hlp, char *dest, size_t dest_buf_size);
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

   /* name of another (helper) param, typically a buffer size */
   const char *helper_param_name;

   /* true if the retval of the func represents the real value of this buffer */
   bool real_sz_in_ret;

   /* invisible: if true, the param won't be dumped (see llseek) */
   bool invisible;
};

enum sys_saved_param_fmt {
   sys_fmt0 = 0,
   sys_fmt1 = 1,
};

struct syscall_info {

   /* syscall number */
   u32 sys_n;

   /* number of parameters */
   s8 n_params;

   /*
    * True if the syscall is expected to block. In other words:
    *    => do we care about the ENTER event?
    *
    * exp_block = false DOES NOT mean that the syscall cannot block due to
    * I/O like sys_rename, sys_link etc. But that that's not interesting 99.9%
    * of the time. It's interesting instead to observe the ENTER and EXIT events
    * for syscalls like read(), write(), select(), poll(), waitpid() etc.
    */
   bool exp_block;

   /* return type of the syscall */
   const struct sys_param_type *ret_type;

   /* info about its parameters */
   struct sys_param_info params[6];
};

void
init_tracing(void);

void
init_trace_printk(void);

bool
read_trace_event(struct trace_event *e, u32 timeout_ticks);

bool
read_trace_event_noblock(struct trace_event *e);

void
trace_syscall_enter_int(u32 sys,
                        ulong a1,
                        ulong a2,
                        ulong a3,
                        ulong a4,
                        ulong a5,
                        ulong a6);
void
trace_syscall_exit_int(u32 sys,
                       long retval,
                       ulong a1,
                       ulong a2,
                       ulong a3,
                       ulong a4,
                       ulong a5,
                       ulong a6);

void
trace_printk_raw_int(short level, const char *buf, size_t buf_size);

ATTR_PRINTF_LIKE(2) void
trace_printk_int(short level, const char *fmt, ...);

void
trace_signal_delivered_int(int target_tid, int signum);

void
trace_task_killed_int(int signum);

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

const char *
get_signal_name(int signum);

int
get_traced_syscalls_count(void);

int
get_traced_tasks_count(void);

void
get_traced_syscalls_str(char *buf, size_t len);

int
set_traced_syscalls(const char *str);

int
tracing_get_in_buffer_events_count(void);

extern const struct syscall_info *tracing_metadata;
extern const struct sys_param_type ptype_int;
extern const struct sys_param_type ptype_voidp;
extern const struct sys_param_type ptype_oct;
extern const struct sys_param_type ptype_errno_or_val;
extern const struct sys_param_type ptype_errno_or_ptr;
extern const struct sys_param_type ptype_buffer;
extern const struct sys_param_type ptype_big_buf;
extern const struct sys_param_type ptype_path;
extern const struct sys_param_type ptype_open_flags;
extern const struct sys_param_type ptype_iov_in;
extern const struct sys_param_type ptype_iov_out;
extern const struct sys_param_type ptype_int32_pair;
extern const struct sys_param_type ptype_doff64;
extern const struct sys_param_type ptype_whence;
extern const struct sys_param_type ptype_u64_ptr;
extern const struct sys_param_type ptype_signum;

static ALWAYS_INLINE bool
tracing_is_enabled_on_sys(u32 sys_n)
{
   extern bool *traced_syscalls;

   if (sys_n >= MAX_SYSCALLS)
      return false;

   return traced_syscalls[sys_n];
}

static ALWAYS_INLINE bool
exp_block(const struct syscall_info *si)
{
   extern bool __force_exp_block;
   return __force_exp_block || si->exp_block;
}

static ALWAYS_INLINE void
tracing_set_enabled(bool val)
{
   extern bool __tracing_on;
   __tracing_on = val;
}

static ALWAYS_INLINE bool
tracing_is_enabled(void)
{
   extern bool __tracing_on;
   return __tracing_on;
   return true;
}

static ALWAYS_INLINE bool
trace_printk_is_enabled(void)
{
   return TRACE_PRINTK_ENABLED_ON_BOOT || tracing_is_enabled();
}

static ALWAYS_INLINE void
tracing_set_force_exp_block(bool enabled)
{
   extern bool __force_exp_block;
   __force_exp_block = enabled;
}

static ALWAYS_INLINE bool
tracing_is_force_exp_block_enabled(void)
{
   extern bool __force_exp_block;
   return __force_exp_block;
}

static ALWAYS_INLINE bool
tracing_are_dump_big_bufs_on(void)
{
   extern bool __tracing_dump_big_bufs;
   return __tracing_dump_big_bufs;
}

static ALWAYS_INLINE void
tracing_set_dump_big_bufs_opt(bool enabled)
{
   extern bool __tracing_dump_big_bufs;
   __tracing_dump_big_bufs = enabled;
}

static ALWAYS_INLINE int
tracing_get_printk_lvl(void)
{
   extern int __tracing_printk_lvl;
   return __tracing_printk_lvl;
}

static ALWAYS_INLINE void
tracing_set_printk_lvl(int lvl)
{
   extern int __tracing_printk_lvl;
   __tracing_printk_lvl = lvl;
}


#define trace_sys_enter(sn, ...)                                               \
   if (MOD_tracing && UNLIKELY(tracing_is_enabled())) {                        \
      trace_syscall_enter_int(sn, __VA_ARGS__);                          \
   }

#define trace_sys_exit(sn, ret, ...)                                           \
   if (MOD_tracing && UNLIKELY(tracing_is_enabled())) {                        \
      trace_syscall_exit_int(sn, (long)(ret), __VA_ARGS__);              \
   }

#define trace_printk(lvl, fmt, ...)                                            \
   if (MOD_tracing && UNLIKELY(trace_printk_is_enabled())) {                   \
      trace_printk_int((lvl), fmt, ##__VA_ARGS__);                             \
   }

#define trace_printk_raw(lvl, buf, buf_sz)                                     \
   if (MOD_tracing && UNLIKELY(trace_printk_is_enabled())) {                   \
      trace_printk_raw_int((lvl), (buf), (buf_sz));                            \
   }


#define trace_signal_delivered(target_tid, signum)                             \
   if (MOD_tracing && UNLIKELY(tracing_is_enabled())) {                        \
      trace_signal_delivered_int(target_tid, signum);                          \
   }

#define trace_task_killed(signum)                                              \
   if (MOD_tracing && UNLIKELY(tracing_is_enabled())) {                        \
      trace_task_killed_int(signum);                                           \
   }
