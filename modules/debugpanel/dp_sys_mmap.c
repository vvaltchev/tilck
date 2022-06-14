/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_kmalloc.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/kmalloc_debug.h>

#include "termutil.h"
#include "dp_int.h"

static int row;

static void dp_dump_mmap(void)
{
   struct mem_region ma;

   dp_writeln("           START                 END        (T, Extr)");

   for (int i = 0; i < get_mem_regions_count(); i++) {

      get_mem_region(i, &ma);

      dp_writeln("%02d) %#018llx - %#018llx (%d, %s) [%8u KB]", i,
                 ma.addr, ma.addr + ma.len - 1,
                 ma.type, mem_region_extra_to_str(ma.extra), ma.len / KB);
   }

   dp_writeln("");
}

#ifdef arch_x86_family

static const char *mtrr_mem_type_str[8] =
{
   [MEM_TYPE_UC] = "UC",
   [MEM_TYPE_WC] = "WC",
   [MEM_TYPE_R1] = "??",
   [MEM_TYPE_R2] = "??",
   [MEM_TYPE_WT] = "WT",
   [MEM_TYPE_WP] = "WP",
   [MEM_TYPE_WB] = "WB",
   [MEM_TYPE_UC_] = "UC-",
};

static void dump_var_mtrrs(void)
{
   if (!get_var_mttrs_count()) {
      dp_writeln("MTRRs: not supported on this CPU");
      return;
   }

   u64 mtrr_dt = rdmsr(MSR_IA32_MTRR_DEF_TYPE);
   dp_writeln("MTRRs (default type: %s):",
              mtrr_mem_type_str[mtrr_dt & 0xff]);

   for (u32 i = 0; i < get_var_mttrs_count(); i++) {

      u64 physBaseVal = rdmsr(MSR_MTRRphysBase0 + 2 * i);
      u64 physMaskVal = rdmsr(MSR_MTRRphysBase0 + 2 * i + 1);
      u8 mem_type = physBaseVal & 0xff;

      if (!(physMaskVal & (1 << 11)))
         continue;

      physBaseVal &= ~0xffu;
      physMaskVal &= ~((u64)PAGE_SIZE - 1);

      u32 first_set_bit = get_first_set_bit_index64(physMaskVal);
      u64 sz = (1ull << first_set_bit) / KB;
      bool one_block = true;

      for (u32 b = first_set_bit; b < x86_cpu_features.phys_addr_bits; b++) {
         if (!(physMaskVal & (1ull << b))) {
            one_block = false;
            break;
         }
      }

      if (one_block) {
         dp_writeln("%02d) 0x%llx %s [%8llu KB]",
                    i, physBaseVal, mtrr_mem_type_str[mem_type], sz);
      } else {
         dp_writeln("%02d) 0x%llx %s [%8s]",
                    i, physBaseVal, mtrr_mem_type_str[mem_type], "???");
      }
   }
}

#endif

static void dump_global_mem_stats(void)
{
   static struct debug_kmalloc_heap_info hi;

   struct mem_region ma;
   u64 tot_usable = 0;
   u64 kernel_mem = 0;
   u64 ramdisk_mem = 0;
   u64 kmalloc_mem = 0;
   u64 tot_used = 0;

   ASSERT(!is_preemption_enabled());

   for (int i = 0; i < KMALLOC_HEAPS_COUNT; i++) {

      if (!debug_kmalloc_get_heap_info(i, &hi))
         break;

      kmalloc_mem += hi.mem_allocated;
   }

   for (int i = 0; i < get_mem_regions_count(); i++) {

      get_mem_region(i, &ma);

      if (ma.type == MULTIBOOT_MEMORY_AVAILABLE ||
          (ma.extra & (MEM_REG_EXTRA_RAMDISK | MEM_REG_EXTRA_KERNEL)))
      {
         tot_usable += ma.len;

         if (ma.extra & MEM_REG_EXTRA_KERNEL)
            kernel_mem += ma.len;

         if (ma.extra & MEM_REG_EXTRA_RAMDISK)
            ramdisk_mem += ma.len;
      }
   }

   kernel_mem -= KMALLOC_FIRST_HEAP_SIZE;
   tot_used = kmalloc_mem + ramdisk_mem + kernel_mem;

   dp_writeln(
      "Total usable physical mem:   %8llu KB [ %s%llu MB ]",
      tot_usable / KB,
      "\033(0g\033(B",              /* plus/minus sign */
      tot_usable / MB
   );

   dp_writeln(
      "Used by kmalloc:             %8llu KB",
      kmalloc_mem / KB
   );

   dp_writeln(
      "Used by initrd:              %8llu KB",
      ramdisk_mem / KB
   );

   dp_writeln(
      "Used by kernel text + data:  %8llu KB",
      kernel_mem / KB
   );

   dp_writeln(
      "Tot used:                    %8llu KB",
      tot_used / KB
   );

   dp_writeln("");
}

static void dp_show_sys_mmap(void)
{
   row = dp_screen_start_row;

   disable_preemption();
   {
      dump_global_mem_stats();
   }
   enable_preemption();

   dp_dump_mmap();

#ifdef arch_x86_family
   dump_var_mtrrs();
#endif

   dp_writeln("");
}

static struct dp_screen dp_memmap_screen =
{
   .index = 1,
   .label = "MemMap",
   .draw_func = dp_show_sys_mmap,
   .on_keypress_func = NULL,
};

__attribute__((constructor))
static void dp_memmap_init(void)
{
   dp_register_screen(&dp_memmap_screen);
}
