/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/utils.h>
#include <tilck/common/string_util.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/paging.h>

#define MTRR_DEF_TYPE_MTRR_ENABLED (1u << 11)
#define MTRR_PHYS_MASK_VALID       (1u << 11)

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

u32 get_var_mttrs_count(void)
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
      bool used = !!(mask_reg & MTRR_DEF_TYPE_MTRR_ENABLED);

      if (!used)
         return (int)selected;
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

struct mtrr_change_ctx {

   uptr eflags;
   uptr cr4;
   uptr cr0;
};

/*
 * As described in:
 * Intel's System Programming Guide (Vol. 3A), Section 11.11.7.2
 */
static void pre_mtrr_change(struct mtrr_change_ctx *ctx)
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

/*
 * As described in:
 * Intel's System Programming Guide (Vol. 3A), Section 11.11.7.2
 */
static void post_mtrr_change(struct mtrr_change_ctx *ctx)
{
   /* Flush all the WB entries in the cache and invalidate the rest */
   write_back_and_invl_cache();

   /* Flush all TLB entries by re-loading CR3 */
   write_cr3(read_cr3());

   enable_mtrr_int();

   cache_enable(&ctx->cr0);

   /* Restore CR4 */
   write_cr4(ctx->cr4);

   enable_interrupts(&ctx->eflags);
}

void set_mtrr(u32 num, u64 paddr, u32 pow2size, u8 mem_type)
{
   ASSERT(num < get_var_mttrs_count());
   ASSERT(pow2size > 0);
   ASSERT(roundup_next_power_of_2(pow2size) == pow2size);
   ASSERT(round_up_at64(paddr, pow2size) == paddr);
   ASSERT(x86_cpu_features.edx1.mtrr);

   struct mtrr_change_ctx ctx;

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

void reset_mtrr(u32 num)
{
   struct mtrr_change_ctx ctx;
   ASSERT(num < get_var_mttrs_count());

   pre_mtrr_change(&ctx);
   {
      wrmsr(MSR_MTRRphysBase0 + 2 * num + 1, 0);
      wrmsr(MSR_MTRRphysBase0 + 2 * num, 0);
   }
   post_mtrr_change(&ctx);
}
