
#include <tilck/common/utils.h>
#include <tilck/common/string_util.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/paging.h>

#define MTRR_DEF_TYPE_MTRR_ENABLED (1 << 11)
#define MTRR_PHYS_MASK_VALID       (1 << 11)

static void enable_mtrr_int(void)
{
   u64 mtrr_dt = rdmsr(MSR_IA32_MTRR_DEF_TYPE);
   mtrr_dt |= MTRR_DEF_TYPE_MTRR_ENABLED;
   wrmsr(MSR_IA32_MTRR_DEF_TYPE, mtrr_dt);
}

static void disable_mtrr_int(void)
{
   u64 mtrr_dt = rdmsr(MSR_IA32_MTRR_DEF_TYPE);
   mtrr_dt &= ~MTRR_DEF_TYPE_MTRR_ENABLED;
   wrmsr(MSR_IA32_MTRR_DEF_TYPE, mtrr_dt);
}

void enable_mtrr(void)
{
   u64 mtrr_dt = rdmsr(MSR_IA32_MTRR_DEF_TYPE);

   if (!(mtrr_dt & MTRR_DEF_TYPE_MTRR_ENABLED)) {
      enable_mtrr_int();
   }

   printk("[CPU features] MTRR enabled\n");
}

int get_var_mttrs_count(void)
{
   if (!x86_cpu_features.edx1.mtrr)
      return 0;

   return rdmsr(MSR_IA32_MTRRCAP) & 255;
}

int get_free_mtrr(void)
{
   u32 var_mtrr_count = get_var_mttrs_count();
   u32 selected = 0;

   for (u32 i = 0; i < var_mtrr_count; i++, selected++) {

      u64 mask_reg = rdmsr(MSR_MTRRphysBase0 + 2 * i + 1);
      bool used = !!(mask_reg & (1 << 11));

      if (!used)
         return selected;
   }

   return -1;
}

static void cache_disable(uptr *saved_cr0)
{
   uptr cr0 = read_cr0();

   *saved_cr0 = cr0;

   cr0 |= CR0_CD;

   /*
    * Clear the NW (not write-through) bit in order to set caching
    * in "No-fill Cache Mode", where the memory coherency is maintained.
    */
   cr0 &= ~CR0_NW;

   write_cr0(cr0);
}

static void cache_enable(uptr *saved_cr0)
{
   write_cr0(*saved_cr0);
}

typedef struct {

   uptr eflags;
   uptr cr4;
   uptr cr0;

} mtrr_change_ctx;

/*
 * As described by Intel's System Programming Guide (Vol. 3A), Section 11.11.7.2
 */
static void pre_mtrr_change(mtrr_change_ctx *ctx)
{
   disable_interrupts(&ctx->eflags);

   /* Save CR4 */
   ctx->cr4 = read_cr4();

   cache_disable(&ctx->cr0);

   /* Flush all the WB entries in the cache and invalidate the rest */
   write_back_and_invl_cache();

   /* Flush all TLB entries by re-loading CR3 */
   write_cr3(read_cr3());

   disable_mtrr_int();
}

static void post_mtrr_change(mtrr_change_ctx *ctx)
{
   /* Flush all the WB entries in the cache and invalidate the rest */
   write_back_and_invl_cache();

   /* Flush all TLB entries by re-loading CR3 */
   write_cr3(read_cr3());

   /* XXX: HERE we fail when we try to reset the MTRR entry 0 */
   enable_mtrr_int();

   cache_enable(&ctx->cr0);

   /* Restore CR4 */
   write_cr4(ctx->cr4);

   enable_interrupts(&ctx->eflags);
}

void set_mtrr(int num, u64 paddr, u32 pow2size, u8 mem_type)
{
   ASSERT(num > 0);
   ASSERT(num < get_var_mttrs_count());
   ASSERT(pow2size > 0);
   ASSERT(roundup_next_power_of_2(pow2size) == pow2size);
   ASSERT(round_up_at64(paddr, pow2size) == paddr);
   ASSERT(x86_cpu_features.edx1.mtrr);

   mtrr_change_ctx ctx;

   pre_mtrr_change(&ctx);
   {
      u64 mask64 = ~((u64)pow2size - 1);
      u64 physBaseVal = ((u64)paddr & PAGE_MASK) | mem_type;
      u64 physMaskVal;

      physMaskVal = mask64 & ((1ull << x86_cpu_features.phys_addr_bits) - 1);
      physMaskVal |= MTRR_PHYS_MASK_VALID;

      wrmsr(MSR_MTRRphysBase0 + 2 * num, physBaseVal);
      wrmsr(MSR_MTRRphysBase0 + 2 * num + 1, physMaskVal);
   }
   post_mtrr_change(&ctx);
}

void reset_mtrr(int num)
{
   mtrr_change_ctx ctx;

   ASSERT(num >= 0);
   ASSERT(num < get_var_mttrs_count());

   pre_mtrr_change(&ctx);
   {
      wrmsr(MSR_MTRRphysBase0 + 2 * num + 1, 0);
      wrmsr(MSR_MTRRphysBase0 + 2 * num, 0);
   }
   post_mtrr_change(&ctx);
}

static const char *mtrr_mem_type_str[8] =
{
   [MEM_TYPE_UC] = "UC",
   [MEM_TYPE_WC] = "WC",
   [MEM_TYPE_R1] = "??",
   [MEM_TYPE_R2] = "??",
   [MEM_TYPE_WT] = "WT",
   [MEM_TYPE_WP] = "WP",
   [MEM_TYPE_WB] = "WB",
   [MEM_TYPE_UC_] = "UC-"
};

void dump_var_mtrrs(void)
{
   if (!get_var_mttrs_count()) {
      printk("MTRRs: not supported on this CPU\n");
      return;
   }

   printk(NO_PREFIX "MTRRs: \n");

   for (int i = 0; i < get_var_mttrs_count(); i++) {

      u64 physBaseVal = rdmsr(MSR_MTRRphysBase0 + 2 * i);
      u64 physMaskVal = rdmsr(MSR_MTRRphysBase0 + 2 * i + 1);
      u8 mem_type = physBaseVal & 0xff;

      if (!(physMaskVal & (1 << 11)))
         continue;

      physBaseVal &= ~0xff;
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
         printk(NO_PREFIX "%02d) 0x%llx %s [%8llu KB]\n",
                i, physBaseVal, mtrr_mem_type_str[mem_type], sz);
      } else {
         printk(NO_PREFIX "%02d) 0x%llx %s [%8s]\n",
                i, physBaseVal, mtrr_mem_type_str[mem_type], "???");
      }
   }
}
