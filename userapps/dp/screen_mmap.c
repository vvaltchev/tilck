/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * MemMap panel: global memory stats + physical memory map regions +
 * (on x86) variable MTRRs. Driven by three TILCK_CMD_DP_GET_*
 * sub-commands and the same multi-section layout the in-kernel
 * modules/debugpanel/dp_sys_mmap.c had.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include "term.h"
#include "tui_layout.h"
#include "dp_int.h"
#include "dp_panel.h"

#define KB_  1024UL
#define MB_  (1024UL * 1024UL)

#define MAX_MEM_REGIONS  64
#define MAX_MTRRS        32

static struct dp_mem_region regions[MAX_MEM_REGIONS];
static int region_count;

static struct dp_mem_global_stats gstats;
static int got_gstats;

static struct dp_mtrr_entry mtrrs[MAX_MTRRS];
static struct dp_mtrr_info mtrr_info;
static int mtrr_count;

static int row;

/*
 * Mirrors the kernel's mem_region.extra encoding. The kernel exposes
 * an enum there but it is internal; we only need to label a few well-
 * known bits in a way that matches what mem_region_extra_to_str()
 * used to print. If the kernel adds new bits, we render them as hex.
 */
#define MEM_REG_EXTRA_RAMDISK     (1u << 0)
#define MEM_REG_EXTRA_KERNEL      (1u << 1)
#define MEM_REG_EXTRA_LOWMEM      (1u << 2)
#define MEM_REG_EXTRA_FRAMEBUFFER (1u << 3)
#define MEM_REG_EXTRA_DMA         (1u << 4)

/* Mirrors kernel/mm/system_mmap.c::mem_region_extra_to_str. Strings
 * are 4 chars to keep MemMap rows column-aligned with the kernel's
 * dp_sys_mmap.c output. Multi-bit combos render as MIXD. */
static const char *
extra_to_str(unsigned extra)
{
   switch (extra) {
      case 0:                          return "    ";
      case MEM_REG_EXTRA_RAMDISK:      return "RDSK";
      case MEM_REG_EXTRA_KERNEL:       return "KRNL";
      case MEM_REG_EXTRA_LOWMEM:       return "LMRS";
      case MEM_REG_EXTRA_FRAMEBUFFER:  return "FBUF";
      case MEM_REG_EXTRA_DMA:          return "DMA ";
   }
   return "MIXD";
}

/* Mirrors x86's MEM_TYPE_* (UC, WC, R1, R2, WT, WP, WB, UC-). */
static const char *mtrr_type_str[8] = {
   "UC ", "WC ", "?? ", "?? ", "WT ", "WP ", "WB ", "UC-",
};

static long dp_cmd_get_mmap(struct dp_mem_region *buf, unsigned long max)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_GET_MEM_MAP,
                  (long)buf, (long)max, 0L, 0L);
}

static long dp_cmd_get_gstats(struct dp_mem_global_stats *out)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_GET_MEM_GLOBAL_STATS,
                  (long)out, 0L, 0L, 0L);
}

static long dp_cmd_get_mtrrs(struct dp_mtrr_entry *buf, unsigned long max,
                             struct dp_mtrr_info *info)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_GET_MTRRS,
                  (long)buf, (long)max, (long)info, 0L);
}

static void dp_mmap_on_enter(void)
{
   long rc = dp_cmd_get_mmap(regions, MAX_MEM_REGIONS);
   region_count = (rc < 0) ? 0 : (int)rc;

   got_gstats = (dp_cmd_get_gstats(&gstats) == 0);

   memset(&mtrr_info, 0, sizeof(mtrr_info));
   long mrc = dp_cmd_get_mtrrs(mtrrs, MAX_MTRRS, &mtrr_info);
   mtrr_count = (mrc < 0) ? 0 : (int)mrc;
}

static void dump_global_stats(void)
{
   if (!got_gstats) {
      dp_writeln(E_COLOR_BR_RED
                 "global mem stats unavailable" RESET_ATTRS);
      return;
   }

   dp_writeln("Total usable physical mem:   %8llu KB [ %s%llu MB ]",
              (unsigned long long)gstats.tot_usable / KB_,
              "\033(0g\033(B",
              (unsigned long long)gstats.tot_usable / MB_);

   dp_writeln("Used by kmalloc:             %8llu KB",
              (unsigned long long)gstats.kmalloc_used / KB_);

   dp_writeln("Used by initrd:              %8llu KB",
              (unsigned long long)gstats.ramdisk_used / KB_);

   dp_writeln("Used by kernel text + data:  %8llu KB",
              (unsigned long long)gstats.kernel_used / KB_);

   const unsigned long long tot =
      gstats.kmalloc_used + gstats.ramdisk_used + gstats.kernel_used;

   dp_writeln("Tot used:                    %8llu KB", tot / KB_);
   dp_writeln(" ");
}

static void dump_regions(void)
{
   dp_writeln("           START                 END        (T, Extr)");

   for (int i = 0; i < region_count; i++) {

      const struct dp_mem_region *r = &regions[i];

      dp_writeln("%02d) 0x%016llx - 0x%016llx (%u, %s) [%8llu KB]",
                 i,
                 (unsigned long long)r->addr,
                 (unsigned long long)(r->addr + r->len - 1),
                 r->type,
                 extra_to_str(r->extra),
                 (unsigned long long)r->len / KB_);
   }

   dp_writeln(" ");
}

static void dump_mtrrs(void)
{
   if (!mtrr_info.supported) {
      dp_writeln("MTRRs: not supported on this CPU");
      return;
   }

   dp_writeln("MTRRs (default type: %s):",
              mtrr_type_str[mtrr_info.default_type & 0x7]);

   for (int i = 0; i < mtrr_count; i++) {

      const struct dp_mtrr_entry *m = &mtrrs[i];

      if (m->one_block) {
         dp_writeln("%02d) 0x%llx %s [%8llu KB]",
                    i,
                    (unsigned long long)m->base,
                    mtrr_type_str[m->mem_type & 0x7],
                    (unsigned long long)m->size_kb);
      } else {
         dp_writeln("%02d) 0x%llx %s [%8s]",
                    i,
                    (unsigned long long)m->base,
                    mtrr_type_str[m->mem_type & 0x7],
                    "???");
      }
   }
}

static void dp_show_mmap(void)
{
   row = tui_screen_start_row;

   dump_global_stats();
   dump_regions();
   dump_mtrrs();
   dp_writeln(" ");
}

static struct dp_screen dp_mmap_screen = {
   .index = 1,
   .label = "MemMap",
   .draw_func = dp_show_mmap,
   .on_dp_enter = dp_mmap_on_enter,
};

__attribute__((constructor))
static void dp_mmap_register(void)
{
   dp_register_screen(&dp_mmap_screen);
}
