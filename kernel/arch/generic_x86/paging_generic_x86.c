/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>
#include <tilck_gen_headers/mod_fb.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/vdso.h>
#include <tilck/kernel/cmdline.h>

#include "paging_generic_x86.h"

u32 *pageframes_refcount;
ulong phys_mem_lim;
struct kmalloc_heap *hi_vmem_heap;

void retain_pageframes_mapped_at(pdir_t *pdir, void *vaddrp, size_t len)
{
   ASSERT(IS_PAGE_ALIGNED(vaddrp));
   ASSERT(IS_PAGE_ALIGNED(len));

   ulong paddr;
   ulong vaddr = (ulong)vaddrp;
   const ulong vaddr_end = vaddr + len;

   for (; vaddr < vaddr_end; vaddr += PAGE_SIZE) {

      if (get_mapping2(pdir, (void *)vaddr, &paddr) < 0)
         continue; /* not mapped, that's fine */

      __pf_ref_count_inc(paddr);
   }
}

void release_pageframes_mapped_at(pdir_t *pdir, void *vaddrp, size_t len)
{
   ASSERT(IS_PAGE_ALIGNED(vaddrp));
   ASSERT(IS_PAGE_ALIGNED(len));

   ulong paddr;
   ulong vaddr = (ulong)vaddrp;
   const ulong vaddr_end = vaddr + len;

   for (; vaddr < vaddr_end; vaddr += PAGE_SIZE) {

      if (get_mapping2(pdir, (void *)vaddr, &paddr) < 0)
         continue; /* not mapped, that's fine */

      __pf_ref_count_dec(paddr);
   }
}

void invalidate_page(ulong vaddr)
{
   invalidate_page_hw(vaddr);
}

void init_paging(void)
{
   int rc;
   void *user_vdso_vaddr;
   size_t pagesframes_refcount_bufsize;

   phys_mem_lim = (ulong)MIN(get_phys_mem_size(),
                             (u64)LINEAR_MAPPING_SIZE);

   /*
    * Allocate the buffer used for keeping a ref-count for each pageframe.
    * This is necessary for COW.
    */

   pagesframes_refcount_bufsize =
      (phys_mem_lim >> PAGE_SHIFT) * sizeof(pageframes_refcount[0]);

   pageframes_refcount = kzmalloc(pagesframes_refcount_bufsize);

   if (!pageframes_refcount) {

      if (in_panic())
         return;        /* We're in panic: silently ignore the failure */

      panic("Unable to allocate pageframes_refcount");
   }

   pf_ref_count_inc(KERNEL_VA_TO_PA(zero_page));

   /* Initialize the kmalloc heap used for the "hi virtual mem" area */
   init_hi_vmem_heap();

   /*
    * Now use the just-created hi vmem heap to reserve a page for the user
    * vdso-like page and expect it to be == USER_VDSO_VADDR.
    */
   user_vdso_vaddr = hi_vmem_reserve(PAGE_SIZE);

   if (user_vdso_vaddr != (void *)USER_VDSO_VADDR)
      panic("user_vdso_vaddr != USER_VDSO_VADDR");

   /*
    * Map a special vdso-like page used for the sysenter interface.
    * This is the only user-mapped page with a vaddr in the kernel space.
    */
   rc = map_page(get_kernel_pdir(),
                 user_vdso_vaddr,
                 KERNEL_VA_TO_PA(&vdso_begin),
                 PAGING_FL_US);

   if (rc < 0)
      panic("Unable to map the vdso-like page");
}

void *
map_framebuffer(pdir_t *pdir,
                ulong paddr,
                ulong vaddr,
                ulong size,
                bool user_mmap)
{
   if (!get_kernel_pdir())
      return failsafe_map_framebuffer(paddr, size);

   if (!pageframes_refcount)
      return failsafe_map_framebuffer(paddr, size);

   size_t count;
   const size_t page_count = pow2_round_up_at(size, PAGE_SIZE) / PAGE_SIZE;
   const u32 pg_flags = PAGING_FL_RW                     |
                        PAGING_FL_SHARED                 |
                        (user_mmap ? PAGING_FL_US : 0);

   if (!vaddr) {

      ASSERT(!user_mmap); /* user mappings always have a vaddr at this layer */
      vaddr = (ulong) hi_vmem_reserve(size);

      if (!vaddr) {

         /*
          * This should NEVER happen. The allocation of the hi vmem does not
          * depend at all from the system. It's all on Tilck. We have 128 MB
          * of virtual space that we can allocate as we want. Unless there's
          * a bug in kmalloc(), we'll never get here.
          */

         if (in_panic()) {

            /*
             * But, in the extremely unlucky case we end up here, there's still
             * one thing we can do, at least to be able to show something on
             * the screen: use a failsafe VADDR for the framebuffer.
             */

            vaddr = FAILSAFE_FB_VADDR;

         } else {

            panic("Unable to reserve hi vmem for the framebuffer");
         }
      }
   }

   count = map_pages(pdir,
                     (void *)vaddr,
                     paddr,
                     page_count,
                     pg_flags);

   if (count < page_count) {

      if (user_mmap) {

         /* This is bad, but not terrible */
         printk("WARNING: unable to mmap framebuffer at %p\n", (void *)vaddr);
         unmap_pages_permissive(pdir, (void *)vaddr, count, false);
         return NULL;
      }

      /*
       * What if this is the only framebuffer available for showing something
       * on the screen? Well, we're screwed. But this should *never* happen.
       */

      panic("Unable to map the framebuffer in the virtual space");
   }

   if (kopt_fb_no_wc) {
      printk("paging: skip marking framebuffer pages as WC (kopt_fb_no_wc)\n");
      return (void *)vaddr;
   }

   if (x86_cpu_features.edx1.pat) {
      size = pow2_round_up_at(size, PAGE_SIZE);
      set_pages_pat_wc(pdir, (void *) vaddr, size);
      return (void *)vaddr;
   }

   if (!x86_cpu_features.edx1.mtrr || user_mmap)
      return (void *)vaddr;

   /*
    * PAT is not available: we have to use MTRRs in order to make the paddr
    * region be of type WC (write-combining).
    */
   int selected_mtrr = get_free_mtrr();
   ulong pow2size = roundup_next_power_of_2(size);

   if (selected_mtrr < 0) {
      /*
       * Show the error, but still don't fail because the framebuffer can work
       * even without setting its memory region to be WC.
       */
      printk("ERROR: No MTRR available for framebuffer");
      return (void *)vaddr;
   }

   if (pow2_round_up_at(paddr, pow2size) != paddr) {
      /* As above, show the error, but DO NOT fail */
      printk("ERROR: paddr (%p) not aligned at power-of-two", TO_PTR(paddr));
      return (void *)vaddr;
   }

   set_mtrr((u32)selected_mtrr, paddr, pow2size, MEM_TYPE_WC);
   return (void *)vaddr;
}

bool hi_vmem_avail(void)
{
   return hi_vmem_heap != NULL;
}

void *hi_vmem_reserve(size_t size)
{
   void *res = NULL;

   disable_preemption();
   {
      if (LIKELY(hi_vmem_heap != NULL))
         res = per_heap_kmalloc(hi_vmem_heap, &size, 0);
   }
   enable_preemption();
   return res;
}

void hi_vmem_release(void *ptr, size_t size)
{
   disable_preemption();
   {
      per_heap_kfree(hi_vmem_heap, ptr, &size, 0);
   }
   enable_preemption();
}

int virtual_read(pdir_t *pdir, void *extern_va, void *dest, size_t len)
{
   int rc;
   disable_preemption();
   {
      rc = virtual_read_unsafe(pdir, extern_va, dest, len);
   }
   enable_preemption();
   return rc;
}

int virtual_write(pdir_t *pdir, void *extern_va, void *src, size_t len)
{
   int rc;
   disable_preemption();
   {
      rc = virtual_write_unsafe(pdir, extern_va, src, len);
   }
   enable_preemption();
   return rc;
}

NODISCARD size_t
map_zero_pages(pdir_t *pdir,
               void *vaddrp,
               size_t page_count,
               u32 pg_flags)
{
   size_t n;
   ulong vaddr = (ulong) vaddrp;

   for (n = 0; n < page_count; n++, vaddr += PAGE_SIZE) {
      if (map_zero_page(pdir, (void *)vaddr, pg_flags) != 0)
         break;
   }

   return n;
}

void handle_page_fault(regs_t *r)
{
   if (in_panic()) {

      printk("Page fault while already in panic state.\n");

      while (true) {
         halt();
      }
   }

   ASSERT(!is_preemption_enabled());
   handle_page_fault_int(r);
}
