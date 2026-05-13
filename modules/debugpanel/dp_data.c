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
#include <tilck/kernel/cmdline.h>         /* kopt_ttys */
#include <tilck/kernel/datetime.h>        /* clock_*resync* */
#include <tilck/kernel/debug_utils.h>     /* register_tilck_cmd */
#include <tilck/kernel/modules.h>         /* REGISTER_MODULE */

#include <tilck/mods/fb_console.h>        /* use_framebuffer + fb_* */

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

/*
 * Defined in dp_debugger.c. Registered as the panic-time mini-
 * debugger via TILCK_CMD_DEBUGGER_TOOL by this module's init below
 * (called from kernel/arch/.../panic.c after setting
 * __in_panic_debugger = true).
 */
int dp_mini_debugger_tool(void);

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

/* ---------------------------- RUNTIME INFO -------------------------- */

static int
tilck_sys_dp_get_runtime_info(ulong u_out, ulong _2, ulong _3, ulong _4)
{
   struct dp_runtime_info out = {0};
   struct clock_resync_stats cstats;

   if (user_out_of_range((void *)u_out, sizeof(out)))
      return -EFAULT;

   out.hypervisor      = (u8)in_hypervisor();
   out.use_framebuffer = (u8)use_framebuffer();
   out.tty_count       = (u32)kopt_ttys;

   if (MOD_fb && use_framebuffer()) {

      struct fb_console_info fbi;
      fb_console_get_info(&fbi);

      out.fb_opt_funcs = (u8)fb_is_using_opt_funcs();
      out.fb_res_x     = fbi.res_x;
      out.fb_res_y     = fbi.res_y;
      out.fb_bpp       = fbi.bpp;
      out.fb_font_w    = fbi.font_w;
      out.fb_font_h    = fbi.font_h;
   }

   out.clk_in_resync      = (u8)clock_in_resync();
   out.clk_in_full_resync = (u8)clock_in_full_resync();

   clock_get_resync_stats(&cstats);
   out.clk_full_resync_count          = cstats.full_resync_count;
   out.clk_full_resync_fail_count     = cstats.full_resync_fail_count;
   out.clk_full_resync_success_count  = cstats.full_resync_success_count;
   out.clk_full_resync_abs_drift_gt_1 = cstats.full_resync_abs_drift_gt_1;
   out.clk_multi_second_resync_count  = cstats.multi_second_resync_count;

   if (copy_to_user((void *)u_out, &out, sizeof(out)))
      return -EFAULT;

   return 0;
}

/* ---------------------------- TRACING ------------------------------- */

/*
 * The TILCK_CMD_DP_TRACE_* and DP_TASK_* sub-commands moved to
 * modules/tracing/tracing_cmd.c — they're tracing infrastructure
 * and need to be available whenever MOD_tracing is built, not only
 * when MOD_debugpanel is built. With this split, dp -t and tracer
 * keep working even if MOD_debugpanel is compiled out.
 */

/* ---------------------------- REGISTRATION -------------------------- */

static void dp_data_register(void)
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
   register_tilck_cmd(TILCK_CMD_DP_GET_RUNTIME_INFO,
                      tilck_sys_dp_get_runtime_info);

   /* The TILCK_CMD_DP_TRACE_* and DP_TASK_* sub-commands are
    * registered by MOD_tracing (modules/tracing/tracing_cmd.c) so
    * the userspace tracer keeps working even if MOD_debugpanel is
    * compiled out. */
}

/* ---------------------------- MODULE INIT --------------------------- */

/*
 * Today the debugpanel module's job in two parts:
 *   1. The TILCK_CMD_DEBUGGER_TOOL slot — the panic-time mini
 *      debugger, invoked from the panic handler. Implementation in
 *      dp_debugger.c.
 *   2. The TILCK_CMD_DP_* data-collection sub-commands consumed by
 *      the userspace `dp` tool (the panels and ps mode). Handlers
 *      in this file; registration via dp_data_register().
 *
 * Slots TILCK_CMD_DEBUG_PANEL / TILCK_CMD_TRACING_TOOL /
 * TILCK_CMD_PS_TOOL (6, 7, 8) used to be registered here too; the
 * full TUI moved to userspace, the slots are now permanent
 * deprecated NULLs and the dispatcher returns -EINVAL.
 */

static void
debugpanel_module_init(void)
{
   register_tilck_cmd(TILCK_CMD_DEBUGGER_TOOL, dp_mini_debugger_tool);
   dp_data_register();
}

static struct module debugpanel_module = {

   .name     = "debugpanel",
   .priority = MOD_dp_prio,
   .init     = &debugpanel_module_init,
};

REGISTER_MODULE(&debugpanel_module);
