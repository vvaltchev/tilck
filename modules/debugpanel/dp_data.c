/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Kernel-side handlers for the TILCK_CMD_DP_* sub-commands of
 * sys_tilck_cmd. Marshal kernel state into the dp_abi.h structs and
 * copy them to userspace. Used by the userspace `dp` tool.
 *
 * Registered into the tilck_cmds[] dispatch table by `dp_data_register()`,
 * called from the debugpanel module's init function in dp.c.
 */

#include <tilck_gen_headers/mod_tracing.h>
#include <tilck_gen_headers/config_kmalloc.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/kmalloc_debug.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/debug_utils.h>     /* register_tilck_cmd */

#include "dp_int.h"

#if MOD_tracing
#include <tilck/mods/tracing.h>

/*
 * The userspace dp_trace_event mirror in <tilck/common/dp_abi.h>
 * MUST stay byte-compatible with the kernel struct trace_event,
 * because the kernel emits trace_event-sized records straight into
 * /syst/tracing/events and userspace reads them as dp_trace_events.
 * Catch any drift at build time.
 */
STATIC_ASSERT(sizeof(struct dp_trace_event) == sizeof(struct trace_event));
STATIC_ASSERT(sizeof(struct dp_syscall_event_data) ==
              sizeof(struct syscall_event_data));
STATIC_ASSERT(sizeof(struct dp_printk_event_data) ==
              sizeof(struct printk_event_data));
#endif

#ifdef arch_x86_family
#include <tilck/kernel/hal.h>
#include <tilck/common/arch/generic_x86/asm_consts.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>
#endif

#define DP_GET_TASKS_HARDCAP   1024
#define DP_GET_CHUNKS_HARDCAP  4096
#define DP_GET_MMAP_HARDCAP     256

void dp_data_register(void);

/* ---------------------------- TASKS --------------------------------- */

static void dp_fill_task_info(struct task *ti, struct dp_task_info *out)
{
   struct process *pi = ti->pi;

   *out = (struct dp_task_info) {
      .tid          = ti->tid,
      .pid          = pi->pid,
      .pgid         = pi->pgid,
      .sid          = pi->sid,
      .parent_pid   = pi->parent_pid,
      .tty          = tty_get_num(pi->proc_tty),
      .state        = (u8)ti->state,
      .stopped      = ti->stopped ? 1 : 0,
      .traced       = ti->traced ? 1 : 0,
      .is_kthread   = is_kernel_thread(ti) ? 1 : 0,
      .is_worker    = (is_kernel_thread(ti) && is_worker_thread(ti))
                      ? 1 : 0,
   };

   if (out->is_worker) {

      int p = wth_get_priority(ti->worker_thread);
      const char *n = wth_get_name(ti->worker_thread);
      snprintk(out->name, sizeof(out->name),
               "<wth:%s(%d)>", n ? n : "generic", p);

   } else if (out->is_kthread) {

      snprintk(out->name, sizeof(out->name), "<%s>", ti->kthread_name);

   } else {

      const char *cmdline =
         pi->debug_cmdline ? pi->debug_cmdline : "<n/a>";
      snprintk(out->name, sizeof(out->name), "%s", cmdline);
   }
}

struct dp_get_tasks_ctx {
   struct dp_task_info *kbuf;
   ulong max;
   ulong count;
};

static int dp_get_tasks_cb(void *obj, void *arg)
{
   struct task *ti = obj;
   struct dp_get_tasks_ctx *ctx = arg;

   if (ti->tid == KERNEL_TID_START)
      return 0;       /* skip the main kernel task */

   if (ctx->count >= ctx->max)
      return 1;       /* stop iteration */

   dp_fill_task_info(ti, &ctx->kbuf[ctx->count++]);
   return 0;
}

static int
tilck_sys_dp_get_tasks(ulong u_buf, ulong max_count, ulong _3, ulong _4)
{
   struct dp_get_tasks_ctx ctx;
   int rc;

   if (max_count == 0)
      return 0;

   if (max_count > DP_GET_TASKS_HARDCAP)
      max_count = DP_GET_TASKS_HARDCAP;

   if (user_out_of_range((void *)u_buf,
                         max_count * sizeof(struct dp_task_info)))
      return -EFAULT;

   ctx.kbuf = kalloc_array_obj(struct dp_task_info, max_count);

   if (!ctx.kbuf)
      return -ENOMEM;

   ctx.max = max_count;
   ctx.count = 0;

   disable_preemption();
   {
      iterate_over_tasks(dp_get_tasks_cb, &ctx);
   }
   enable_preemption();

   rc = copy_to_user((void *)u_buf,
                     ctx.kbuf,
                     ctx.count * sizeof(struct dp_task_info));

   kfree_array_obj(ctx.kbuf, struct dp_task_info, max_count);

   if (rc)
      return -EFAULT;

   return (int)ctx.count;
}

/* ---------------------------- HEAPS --------------------------------- */

static int
tilck_sys_dp_get_heaps(ulong u_buf, ulong max_count, ulong u_stats, ulong _4)
{
   struct dp_heap_info kbuf[KMALLOC_HEAPS_COUNT];
   struct debug_kmalloc_stats kstats;
   struct dp_small_heaps_stats us;
   struct debug_kmalloc_heap_info hi;
   ulong count = 0;
   int rc;

   if (max_count > KMALLOC_HEAPS_COUNT)
      max_count = KMALLOC_HEAPS_COUNT;

   if (max_count > 0 &&
       user_out_of_range((void *)u_buf,
                         max_count * sizeof(struct dp_heap_info)))
      return -EFAULT;

   if (u_stats &&
       user_out_of_range((void *)u_stats, sizeof(struct dp_small_heaps_stats)))
      return -EFAULT;

   for (int i = 0; i < KMALLOC_HEAPS_COUNT; i++) {

      if (!debug_kmalloc_get_heap_info(i, &hi))
         break;

      if (count >= max_count)
         break;

      kbuf[count++] = (struct dp_heap_info) {
         .vaddr            = hi.vaddr,
         .size             = hi.size,
         .mem_allocated    = hi.mem_allocated,
         .min_block_size   = (u32)hi.min_block_size,
         .alloc_block_size = (u32)hi.alloc_block_size,
         .region           = hi.region,
      };
   }

   if (max_count > 0) {

      rc = copy_to_user((void *)u_buf, kbuf,
                        count * sizeof(struct dp_heap_info));
      if (rc)
         return -EFAULT;
   }

   if (u_stats) {

      debug_kmalloc_get_stats(&kstats);

      us = (struct dp_small_heaps_stats) {
         .tot_count           = kstats.small_heaps.tot_count,
         .peak_count          = kstats.small_heaps.peak_count,
         .not_full_count      = kstats.small_heaps.not_full_count,
         .peak_not_full_count = kstats.small_heaps.peak_not_full_count,
      };

      rc = copy_to_user((void *)u_stats, &us, sizeof(us));
      if (rc)
         return -EFAULT;
   }

   return (int)count;
}

/* -------------------------- KMALLOC CHUNKS -------------------------- */

static int
tilck_sys_dp_get_kmalloc_chunks(ulong u_buf, ulong max_count,
                                ulong _3, ulong _4)
{
   struct dp_kmalloc_chunk *kbuf;
   struct debug_kmalloc_chunks_ctx ctx;
   ulong count = 0;
   size_t s, c;
   int rc;

   if (!KRN_KMALLOC_HEAVY_STATS)
      return -EOPNOTSUPP;

   if (max_count == 0)
      return 0;

   if (max_count > DP_GET_CHUNKS_HARDCAP)
      max_count = DP_GET_CHUNKS_HARDCAP;

   if (user_out_of_range((void *)u_buf,
                         max_count * sizeof(struct dp_kmalloc_chunk)))
      return -EFAULT;

   kbuf = kalloc_array_obj(struct dp_kmalloc_chunk, max_count);

   if (!kbuf)
      return -ENOMEM;

   disable_preemption();
   {
      debug_kmalloc_chunks_stats_start_read(&ctx);
      while (debug_kmalloc_chunks_stats_next(&ctx, &s, &c)) {

         if (count >= max_count)
            break;

         const u64 waste = (u64)(
            UNSAFE_MAX(SMALL_HEAP_MBS, roundup_next_power_of_2(s)) - s
         ) * c;

         kbuf[count++] = (struct dp_kmalloc_chunk) {
            .size        = s,
            .count       = c,
            .max_waste   = waste,
            .max_waste_p = (u32)(waste * 1000 /
                                 (waste + (u64)s * (u64)c)),
         };
      }
   }
   enable_preemption();

   rc = copy_to_user((void *)u_buf, kbuf,
                     count * sizeof(struct dp_kmalloc_chunk));

   kfree_array_obj(kbuf, struct dp_kmalloc_chunk, max_count);

   if (rc)
      return -EFAULT;

   return (int)count;
}

/* ---------------------------- IRQ STATS ------------------------------ */

static int
tilck_sys_dp_get_irq_stats(ulong u_out, ulong _2, ulong _3, ulong _4)
{
   extern u32 spur_irq_count;
   extern u32 unhandled_irq_count[256];

   struct dp_irq_stats out = {0};
   u32 mask = 0;

   if (user_out_of_range((void *)u_out, sizeof(out)))
      return -EFAULT;

   if (KRN_TRACK_NESTED_INTERR) {
      extern u32 slow_timer_irq_handler_count;
      out.slow_timer_count = slow_timer_irq_handler_count;
   }

   out.spur_irq_count    = spur_irq_count;
   out.ticks_at_snapshot = get_ticks();

   STATIC_ASSERT(ARRAY_SIZE(out.unhandled_count) ==
                 ARRAY_SIZE(unhandled_irq_count));

   for (u32 i = 0; i < ARRAY_SIZE(unhandled_irq_count); i++)
      out.unhandled_count[i] = unhandled_irq_count[i];

   for (int i = 0; i < 16; i++)
      if (!irq_is_masked(i))
         mask |= (1u << i);

   out.unmasked_mask_lo16 = mask;

   if (copy_to_user((void *)u_out, &out, sizeof(out)))
      return -EFAULT;

   return 0;
}

/* ---------------------------- MEM MAP ------------------------------- */

static int
tilck_sys_dp_get_mem_map(ulong u_buf, ulong max_count, ulong _3, ulong _4)
{
   struct dp_mem_region *kbuf;
   struct mem_region ma;
   ulong total;
   ulong count = 0;
   int rc;

   if (max_count == 0)
      return 0;

   if (max_count > DP_GET_MMAP_HARDCAP)
      max_count = DP_GET_MMAP_HARDCAP;

   if (user_out_of_range((void *)u_buf,
                         max_count * sizeof(struct dp_mem_region)))
      return -EFAULT;

   kbuf = kalloc_array_obj(struct dp_mem_region, max_count);

   if (!kbuf)
      return -ENOMEM;

   total = (ulong)get_mem_regions_count();

   for (ulong i = 0; i < total && count < max_count; i++) {

      get_mem_region((int)i, &ma);

      kbuf[count++] = (struct dp_mem_region) {
         .addr  = ma.addr,
         .len   = ma.len,
         .type  = ma.type,
         .extra = ma.extra,
      };
   }

   rc = copy_to_user((void *)u_buf, kbuf,
                     count * sizeof(struct dp_mem_region));

   kfree_array_obj(kbuf, struct dp_mem_region, max_count);

   if (rc)
      return -EFAULT;

   return (int)count;
}

static int
tilck_sys_dp_get_mem_global_stats(ulong u_out, ulong _2, ulong _3, ulong _4)
{
   struct dp_mem_global_stats out = {0};
   struct debug_kmalloc_heap_info hi;
   struct mem_region ma;

   if (user_out_of_range((void *)u_out, sizeof(out)))
      return -EFAULT;

   disable_preemption();
   {
      for (int i = 0; i < KMALLOC_HEAPS_COUNT; i++) {

         if (!debug_kmalloc_get_heap_info(i, &hi))
            break;

         out.kmalloc_used += hi.mem_allocated;
      }

      for (int i = 0; i < get_mem_regions_count(); i++) {

         get_mem_region(i, &ma);

         if (ma.type == MULTIBOOT_MEMORY_AVAILABLE ||
             (ma.extra & (MEM_REG_EXTRA_RAMDISK | MEM_REG_EXTRA_KERNEL)))
         {
            out.tot_usable += ma.len;

            if (ma.extra & MEM_REG_EXTRA_KERNEL)
               out.kernel_used += ma.len;

            if (ma.extra & MEM_REG_EXTRA_RAMDISK)
               out.ramdisk_used += ma.len;
         }
      }
   }
   enable_preemption();

   if (out.kernel_used >= KMALLOC_FIRST_HEAP_SIZE)
      out.kernel_used -= KMALLOC_FIRST_HEAP_SIZE;

   if (copy_to_user((void *)u_out, &out, sizeof(out)))
      return -EFAULT;

   return 0;
}

/* ---------------------------- MTRRs --------------------------------- */

#ifdef arch_x86_family

static int
tilck_sys_dp_get_mtrrs(ulong u_buf, ulong max_count, ulong u_info, ulong _4)
{
   struct dp_mtrr_entry *kbuf = NULL;
   struct dp_mtrr_info info = {0};
   const u32 n = get_var_mttrs_count();
   ulong count = 0;
   int rc;

   if (u_info && user_out_of_range((void *)u_info, sizeof(info)))
      return -EFAULT;

   if (max_count > 0 &&
       user_out_of_range((void *)u_buf,
                         max_count * sizeof(struct dp_mtrr_entry)))
      return -EFAULT;

   info.supported = (n > 0) ? 1 : 0;

   if (info.supported) {

      u64 dt = rdmsr(MSR_IA32_MTRR_DEF_TYPE);
      info.default_type = (u8)(dt & 0xff);
   }

   if (max_count > 0 && info.supported) {

      kbuf = kalloc_array_obj(struct dp_mtrr_entry, max_count);

      if (!kbuf)
         return -ENOMEM;

      for (u32 i = 0; i < n && count < max_count; i++) {

         u64 base = rdmsr(MSR_MTRRphysBase0 + 2 * i);
         u64 mask = rdmsr(MSR_MTRRphysBase0 + 2 * i + 1);

         if (!(mask & (1u << 11)))
            continue;

         u8 mtype = (u8)(base & 0xff);
         base &= ~0xffull;
         mask &= ~((u64)PAGE_SIZE - 1);

         u32 first_set_bit = get_first_set_bit_index64(mask);
         u64 size_kb = (1ull << first_set_bit) / KB;
         bool one_block = true;

         for (u32 b = first_set_bit;
              b < x86_cpu_features.phys_addr_bits; b++)
         {
            if (!(mask & (1ull << b))) {
               one_block = false;
               break;
            }
         }

         kbuf[count++] = (struct dp_mtrr_entry) {
            .base      = base,
            .size_kb   = one_block ? size_kb : 0,
            .mem_type  = mtype,
            .one_block = one_block ? 1 : 0,
         };
      }

      rc = copy_to_user((void *)u_buf, kbuf,
                        count * sizeof(struct dp_mtrr_entry));

      kfree_array_obj(kbuf, struct dp_mtrr_entry, max_count);

      if (rc)
         return -EFAULT;
   }

   if (u_info && copy_to_user((void *)u_info, &info, sizeof(info)))
      return -EFAULT;

   return (int)count;
}

#else  /* !arch_x86_family */

static int
tilck_sys_dp_get_mtrrs(ulong u_buf, ulong max_count, ulong u_info, ulong _4)
{
   struct dp_mtrr_info info = {0};

   if (u_info) {
      if (user_out_of_range((void *)u_info, sizeof(info)))
         return -EFAULT;
      if (copy_to_user((void *)u_info, &info, sizeof(info)))
         return -EFAULT;
   }

   return -EOPNOTSUPP;
}

#endif

/* ---------------------------- TRACING ------------------------------- */

#if MOD_tracing

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

#else  /* !MOD_tracing */

static int
tilck_sys_dp_trace_set_filter(ulong _1, ulong _2, ulong _3, ulong _4)
{
   return -EOPNOTSUPP;
}

static int
tilck_sys_dp_trace_get_filter(ulong _1, ulong _2, ulong _3, ulong _4)
{
   return -EOPNOTSUPP;
}

static int
tilck_sys_dp_task_set_traced(ulong _1, ulong _2, ulong _3, ulong _4)
{
   return -EOPNOTSUPP;
}

static int
tilck_sys_dp_trace_get_stats(ulong _1, ulong _2, ulong _3, ulong _4)
{
   return -EOPNOTSUPP;
}

static int
tilck_sys_dp_trace_get_sys_name(ulong _1, ulong _2, ulong _3, ulong _4)
{
   return -EOPNOTSUPP;
}

static int
tilck_sys_dp_trace_set_enabled(ulong _1, ulong _2, ulong _3, ulong _4)
{
   return -EOPNOTSUPP;
}

#endif

/* ---------------------------- REGISTRATION -------------------------- */

void dp_data_register(void)
{
   register_tilck_cmd(TILCK_CMD_DP_GET_TASKS,
                      tilck_sys_dp_get_tasks);
   register_tilck_cmd(TILCK_CMD_DP_GET_HEAPS,
                      tilck_sys_dp_get_heaps);
   register_tilck_cmd(TILCK_CMD_DP_GET_KMALLOC_CHUNKS,
                      tilck_sys_dp_get_kmalloc_chunks);
   register_tilck_cmd(TILCK_CMD_DP_GET_IRQ_STATS,
                      tilck_sys_dp_get_irq_stats);
   register_tilck_cmd(TILCK_CMD_DP_GET_MEM_MAP,
                      tilck_sys_dp_get_mem_map);
   register_tilck_cmd(TILCK_CMD_DP_GET_MEM_GLOBAL_STATS,
                      tilck_sys_dp_get_mem_global_stats);
   register_tilck_cmd(TILCK_CMD_DP_GET_MTRRS,
                      tilck_sys_dp_get_mtrrs);
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
}
