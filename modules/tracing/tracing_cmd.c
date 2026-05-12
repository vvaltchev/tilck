/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * TILCK_CMD_DP_TRACE_* and TILCK_CMD_DP_TASK_* sub-command handlers
 * used by the userspace `dp -t` / `tracer` tool.
 *
 * These handlers used to live in modules/debugpanel/dp_data.c, but
 * they're really tracing infrastructure — they read tracing globals
 * (filter, force_exp_block, dump_big_bufs, printk level), call
 * tracing.c helpers, and toggle per-task .traced flags. Putting them
 * here means `dp -t` keeps working when MOD_debugpanel is compiled
 * out: the tracer screen needs only MOD_tracing for events,
 * metadata, and these handlers.
 *
 * The TILCK_CMD_DP_GET_* (per-panel data accessors used by the full
 * dp panel) stay in modules/debugpanel/dp_data.c — those panels are
 * dp UI features, not tracer features.
 */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/debug_utils.h>     /* register_tilck_cmd */

#include <tilck/mods/tracing.h>

void tracing_register_dp_cmd_handlers(void);

static int
tilck_sys_dp_trace_set_filter(ulong u_expr, ulong _2, ulong _3, ulong _4)
{
   char buf[TRACED_SYSCALLS_STR_LEN];
   int rc;

   rc = copy_str_from_user(buf, (void *)u_expr, sizeof(buf) - 1, NULL);

   if (rc != 0)
      return rc < 0 ? rc : -ENAMETOOLONG;

   buf[sizeof(buf) - 1] = '\0';
   return set_traced_syscalls(buf);
}

static int
tilck_sys_dp_trace_get_filter(ulong u_buf, ulong buf_size,
                              ulong _3, ulong _4)
{
   char kbuf[TRACED_SYSCALLS_STR_LEN];
   ulong cap;

   if (buf_size == 0)
      return 0;

   if (user_out_of_range((void *)u_buf, buf_size))
      return -EFAULT;

   get_traced_syscalls_str(kbuf, sizeof(kbuf));
   kbuf[sizeof(kbuf) - 1] = '\0';

   cap = strlen(kbuf) + 1;
   if (cap > buf_size)
      cap = buf_size;

   if (copy_to_user((void *)u_buf, kbuf, cap))
      return -EFAULT;

   return (int)(cap - 1);
}

static int
tilck_sys_dp_task_set_traced(ulong tid, ulong enabled, ulong _3, ulong _4)
{
   struct task *ti;
   int rc = 0;

   if ((int)tid <= 0)
      return -ESRCH;

   if ((int)tid == get_curr_tid())
      return -EPERM;

   if ((int)tid >= KERNEL_TID_START)
      return -EPERM;

   disable_preemption();
   {
      ti = get_task((int)tid);

      if (!ti)
         rc = -ESRCH;
      else
         ti->traced = enabled ? true : false;
   }
   enable_preemption();

   return rc;
}

static int
tilck_sys_dp_trace_get_stats(ulong u_out, ulong _2, ulong _3, ulong _4)
{
   struct dp_trace_stats out = {
      .force_exp_block    = tracing_is_force_exp_block_enabled() ? 1 : 0,
      .dump_big_bufs      = tracing_are_dump_big_bufs_on() ? 1 : 0,
      .enabled            = tracing_is_enabled() ? 1 : 0,
      .printk_lvl         = tracing_get_printk_lvl(),
      .sys_traced_count   = get_traced_syscalls_count(),
      .tasks_traced_count = get_traced_tasks_count(),
   };

   if (user_out_of_range((void *)u_out, sizeof(out)))
      return -EFAULT;

   if (copy_to_user((void *)u_out, &out, sizeof(out)))
      return -EFAULT;

   return 0;
}

static int
tilck_sys_dp_trace_get_sys_name(ulong sys_n, ulong u_buf,
                                ulong buf_sz, ulong _4)
{
   const char *name;
   size_t len, cap;

   if (buf_sz == 0)
      return -EINVAL;

   if (user_out_of_range((void *)u_buf, buf_sz))
      return -EFAULT;

   name = tracing_get_syscall_name((u32)sys_n);

   if (!name)
      return -ENOENT;

   len = strlen(name);
   cap = (len + 1 < buf_sz) ? len + 1 : buf_sz;

   if (copy_to_user((void *)u_buf, name, cap))
      return -EFAULT;

   /* Force NUL termination at the last byte if the name had to be
    * truncated. */
   if (cap == buf_sz) {
      char zero = '\0';
      if (copy_to_user((char *)u_buf + buf_sz - 1, &zero, 1))
         return -EFAULT;
   }

   return (int)(cap - 1);   /* length excluding the trailing NUL */
}

static int
tilck_sys_dp_trace_set_enabled(ulong enabled, ulong _2, ulong _3, ulong _4)
{
   tracing_set_enabled(enabled ? true : false);
   return 0;
}

static int
tilck_sys_dp_trace_set_force_exp_block(ulong v, ulong _2, ulong _3, ulong _4)
{
   tracing_set_force_exp_block(v ? true : false);
   return 0;
}

static int
tilck_sys_dp_trace_set_dump_big_bufs(ulong v, ulong _2, ulong _3, ulong _4)
{
   tracing_set_dump_big_bufs_opt(v ? true : false);
   return 0;
}

static int
tilck_sys_dp_trace_set_printk_lvl(ulong lvl, ulong _2, ulong _3, ulong _4)
{
   if ((long)lvl < 0 || (long)lvl > 100)
      return -EINVAL;

   tracing_set_printk_lvl((int)lvl);
   return 0;
}

/*
 * Returns a per-syscall 0/1 byte bitmap (1 byte per syscall, value 0
 * or 1) up to MIN(buf_sz, MAX_SYSCALLS) bytes. The userspace tracer
 * uses this to render the "Traced syscalls list" ('l' command).
 */
static int
tilck_sys_dp_trace_get_traced_bitmap(ulong u_buf, ulong buf_sz,
                                     ulong _3, ulong _4)
{
   u8 tmp[64];
   ulong copied = 0;
   ulong remaining;

   if (!buf_sz)
      return -EINVAL;

   if (user_out_of_range((void *)u_buf, buf_sz))
      return -EFAULT;

   remaining = MIN(buf_sz, (ulong)MAX_SYSCALLS);

   for (u32 i = 0; copied < remaining; ) {

      ulong chunk = MIN(sizeof(tmp), remaining - copied);

      for (ulong k = 0; k < chunk; k++, i++)
         tmp[k] = tracing_is_enabled_on_sys(i) ? 1 : 0;

      if (copy_to_user((u8 *)u_buf + copied, tmp, chunk))
         return -EFAULT;

      copied += chunk;
   }

   return (int)copied;
}

static int
tilck_sys_dp_trace_get_in_buf_count(ulong _1, ulong _2, ulong _3, ulong _4)
{
   return tracing_get_in_buffer_events_count();
}

/*
 * Walk every task and for each `task.traced == true`: write its tid
 * into the user buffer AND clear the traced flag in-kernel
 * atomically. The clear-and-collect pair makes master's edit-traced-
 * PIDs flow race-free: the user is about to type a new comma-
 * separated list of TIDs that fully replaces the old one.
 *
 * Returns the number of TIDs written to the buffer (extra traced
 * tasks past `max` are still cleared even though they're not
 * reported back).
 */
static int
tilck_sys_dp_task_get_traced_tids_and_clear_cb(void *obj, void *arg)
{
   struct task *ti = obj;
   struct {
      s32 *buf;
      ulong max;
      ulong written;
   } *ctx = arg;

   if (!ti->traced)
      return 0;

   if (ctx->written < ctx->max) {
      ctx->buf[ctx->written] = ti->tid;
      ctx->written++;
   }

   ti->traced = false;
   return 0;
}

static int
tilck_sys_dp_task_get_traced_tids_and_clear(ulong u_buf, ulong max,
                                            ulong _3, ulong _4)
{
   s32 *kbuf;
   ulong sz;
   int rc = 0;
   struct {
      s32 *buf;
      ulong max;
      ulong written;
   } ctx;

   if (!max || max > 1024)
      return -EINVAL;

   sz = max * sizeof(s32);

   if (user_out_of_range((void *)u_buf, sz))
      return -EFAULT;

   kbuf = kzmalloc(sz);

   if (!kbuf)
      return -ENOMEM;

   ctx.buf = kbuf;
   ctx.max = max;
   ctx.written = 0;

   disable_preemption();
   {
      iterate_over_tasks(tilck_sys_dp_task_get_traced_tids_and_clear_cb, &ctx);
   }
   enable_preemption();

   if (ctx.written) {
      if (copy_to_user((void *)u_buf, kbuf, ctx.written * sizeof(s32))) {
         rc = -EFAULT;
         goto out;
      }
   }

   rc = (int)ctx.written;

out:
   kfree2(kbuf, sz);
   return rc;
}

/* ---------------------- test-only injection path -------------------- *
 *
 * `tracer --test` (Tier 2) exercises the trace pipeline by pushing
 * synthetic events through TILCK_CMD_DP_TRACE_INJECT_EVENT instead of
 * triggering them via real syscalls. To avoid an untrusted writer
 * flooding the ring on a release build, the inject handler is gated
 * on a kernel flag that the test driver flips on explicitly via
 * TILCK_CMD_DP_TRACE_SET_TEST_MODE. The flag stays off by default
 * and is reset on tracer-test exit; injection from a process that
 * forgot the toggle gets -EPERM.
 */
static bool __tracing_test_mode;

static int
tilck_sys_dp_trace_set_test_mode(ulong on, ulong _2, ulong _3, ulong _4)
{
   __tracing_test_mode = on ? true : false;
   return 0;
}

static int
tilck_sys_dp_trace_inject_event(ulong u_event, ulong _2, ulong _3, ulong _4)
{
   struct trace_event ev;

   if (!__tracing_test_mode)
      return -EPERM;

   if (user_out_of_range((void *)u_event, sizeof(ev)))
      return -EFAULT;

   if (copy_from_user(&ev, (void *)u_event, sizeof(ev)))
      return -EFAULT;

   tracing_inject_event(&ev);
   return 0;
}

void tracing_register_dp_cmd_handlers(void)
{
   register_tilck_cmd(TILCK_CMD_DP_TRACE_SET_FILTER,
                      tilck_sys_dp_trace_set_filter);
   register_tilck_cmd(TILCK_CMD_DP_TRACE_GET_FILTER,
                      tilck_sys_dp_trace_get_filter);
   register_tilck_cmd(TILCK_CMD_DP_TASK_SET_TRACED,
                      tilck_sys_dp_task_set_traced);
   register_tilck_cmd(TILCK_CMD_DP_TRACE_GET_STATS,
                      tilck_sys_dp_trace_get_stats);
   register_tilck_cmd(TILCK_CMD_DP_TRACE_GET_SYS_NAME,
                      tilck_sys_dp_trace_get_sys_name);
   register_tilck_cmd(TILCK_CMD_DP_TRACE_SET_ENABLED,
                      tilck_sys_dp_trace_set_enabled);
   register_tilck_cmd(TILCK_CMD_DP_TRACE_SET_FORCE_EXP_BLOCK,
                      tilck_sys_dp_trace_set_force_exp_block);
   register_tilck_cmd(TILCK_CMD_DP_TRACE_SET_DUMP_BIG_BUFS,
                      tilck_sys_dp_trace_set_dump_big_bufs);
   register_tilck_cmd(TILCK_CMD_DP_TRACE_SET_PRINTK_LVL,
                      tilck_sys_dp_trace_set_printk_lvl);
   register_tilck_cmd(TILCK_CMD_DP_TRACE_GET_TRACED_BITMAP,
                      tilck_sys_dp_trace_get_traced_bitmap);
   register_tilck_cmd(TILCK_CMD_DP_TRACE_GET_IN_BUF_COUNT,
                      tilck_sys_dp_trace_get_in_buf_count);
   register_tilck_cmd(TILCK_CMD_DP_TASK_GET_TRACED_TIDS_AND_CLEAR,
                      tilck_sys_dp_task_get_traced_tids_and_clear);
   register_tilck_cmd(TILCK_CMD_DP_TRACE_SET_TEST_MODE,
                      tilck_sys_dp_trace_set_test_mode);
   register_tilck_cmd(TILCK_CMD_DP_TRACE_INJECT_EVENT,
                      tilck_sys_dp_trace_inject_event);
}
