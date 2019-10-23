/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/system_mmap.h>

#include "termutil.h"
#include "dp_int.h"

static int row;

static const char *mem_region_extra_to_str(u32 e)
{
   switch (e) {
      case MEM_REG_EXTRA_RAMDISK:
         return "RDSK";
      case MEM_REG_EXTRA_KERNEL:
         return "KRNL";
      case MEM_REG_EXTRA_LOWMEM:
         return "LMRS";
      case MEM_REG_EXTRA_FRAMEBUFFER:
         return "FBUF";
      default:
         return "    ";
   }
}

static void dump_memory_map(struct memory_region_t *regions, int count)
{
   dp_writeln("           START                 END        (T, Extr)");

   for (int i = 0; i < count; i++) {

      struct memory_region_t *ma = regions + i;

      dp_writeln("%02d) 0x%016llx - 0x%016llx (%d, %s) [%8u KB]", i,
                 ma->addr, ma->addr + ma->len,
                 ma->type, mem_region_extra_to_str(ma->extra), ma->len / KB);
   }

   dp_writeln("");
}

#ifdef __arch__x86__

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

static void dp_show_sys_mmap(void)
{
   row = dp_screen_start_row;
   dump_memory_map(mem_regions, mem_regions_count);

#ifdef __arch__x86__
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
