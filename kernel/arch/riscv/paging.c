/* SPDX-License-Identifier: BSD-2-Clause */
#include <tilck_gen_headers/config_mm.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/vdso.h>
#include <tilck/kernel/cmdline.h>

#include <tilck/mods/tracing.h>

#include <sys/mman.h>      // system header
#include "paging_int.h"
#include "paging_generic.h"

ulong kernel_va_pa_offset;
ulong linear_va_pa_offset;

pdir_t *__kernel_pdir;

#define EARLY_PT_NUM 4
static page_table_t early_pt[EARLY_PT_NUM] ALIGNED_AT(PAGE_SIZE);

static page_table_t *alloc_early_pt(void)
{
   static int early_pt_used = 0;

   if (early_pt_used >= EARLY_PT_NUM)
      return NULL;

   return &early_pt[early_pt_used++];
}

bool handle_potential_cow(void *context)
{
   NOT_IMPLEMENTED();
}

void handle_page_fault_int(regs_t *r)
{
   NOT_IMPLEMENTED();
}

bool is_mapped(pdir_t *pdir, void *vaddrp)
{
   NOT_IMPLEMENTED();
}

bool is_rw_mapped(pdir_t *pdir, void *vaddrp)
{
   NOT_IMPLEMENTED();
}

void set_page_rw(pdir_t *pdir, void *vaddrp, bool rw)
{
   NOT_IMPLEMENTED();
}

void
unmap_page(pdir_t *pdir, void *vaddrp, bool free_pageframe)
{
   NOT_IMPLEMENTED();
}

int
unmap_page_permissive(pdir_t *pdir, void *vaddrp, bool free_pageframe)
{
   NOT_IMPLEMENTED();
}

void
unmap_pages(pdir_t *pdir,
            void *vaddr,
            size_t page_count,
            bool do_free)
{
   NOT_IMPLEMENTED();
}

size_t
unmap_pages_permissive(pdir_t *pdir,
                       void *vaddr,
                       size_t page_count,
                       bool do_free)
{
   NOT_IMPLEMENTED();
}

ulong get_mapping(pdir_t *pdir, void *vaddrp)
{
   NOT_IMPLEMENTED();
}

int get_mapping2(pdir_t *pdir, void *vaddrp, ulong *pa_ref)
{
   NOT_IMPLEMENTED();
}

NODISCARD int
map_page_int(pdir_t *pdir, void *vaddrp, ulong paddr, u32 hw_flags)
{
   NOT_IMPLEMENTED();
}

NODISCARD size_t
map_pages_int(pdir_t *pdir,
              void *vaddr,
              ulong paddr,
              size_t page_count,
              bool big_pages_allowed,
              u32 hw_flags)
{
   NOT_IMPLEMENTED();
}

NODISCARD int
map_page(pdir_t *pdir, void *vaddrp, ulong paddr, u32 pg_flags)
{
   NOT_IMPLEMENTED();
}



NODISCARD int
map_zero_page(pdir_t *pdir, void *vaddrp, u32 pg_flags)
{
   NOT_IMPLEMENTED();
}

NODISCARD size_t
map_pages(pdir_t *pdir,
          void *vaddr,
          ulong paddr,
          size_t page_count,
          u32 pg_flags)
{
   NOT_IMPLEMENTED();
}

pdir_t *pdir_clone(pdir_t *pdir)
{
   NOT_IMPLEMENTED();
}

void pdir_destroy(pdir_t *pdir)
{
   NOT_IMPLEMENTED();
}

pdir_t *
pdir_deep_clone(pdir_t *pdir)
{
   NOT_IMPLEMENTED();
}

void map_big_page_int(pdir_t *pdir,
                      void *vaddrp,
                      ulong paddr,
                      u32 hw_flags)
{
   NOT_IMPLEMENTED();
}

/*
 * Used in early boot(before kmalloc initialization),
 * only big pages are mapped.
 */
static inline void
create_early_page_table(pdir_t *pdir,
                        ulong vaddr,
                        ulong paddr,
                        size_t lenth,
                        bool in_vm)
{
   ulong end_va = vaddr + lenth;
   ulong pmd_paddr;
   page_t *e;
   page_table_t *tmpdir;

   for (; vaddr < end_va;) {

      tmpdir = pdir;

      for (int level = RV_PAGE_LEVEL; level > 0; level--) {

         e = &tmpdir->entries[PTE_INDEX(level, vaddr)];

         if ((level > 1) && (!e->present)) {

            if (in_vm)
               pmd_paddr = KERNEL_VA_TO_PA((ulong)alloc_early_pt());
            else
               pmd_paddr = (ulong)alloc_early_pt();

            e->raw = (PFN(pmd_paddr) << _PAGE_PFN_SHIFT) | PAGE_TABLE;

            tmpdir = (page_table_t *)pmd_paddr;
         } else {

            tmpdir = (void *)((ulong)e->pfn << PAGE_SHIFT);
         }
      }

      e->raw = MAKE_BIG_PAGE(paddr);

      vaddr += L1_PAGE_SIZE;
      paddr += L1_PAGE_SIZE;
   }
}

void *init_early_mapping(ulong fdt_paddr)
{
   extern char _start;

   pdir_t *pgd = (void *)&page_size_buf[0];
   ulong kernel_pa = (ulong)&_start - 0x1000;
   ulong text_offset = ((struct linux_image_h *)(void *)&_start)->text_offset;
   ulong base_pa = kernel_pa - text_offset;

   kernel_va_pa_offset = KERNEL_BASE_VA - base_pa;
   linear_va_pa_offset = BASE_VA - base_pa;

   /* Copy the flattened device tree to 1MB below kernel's head */
   memcpy((void *)(kernel_pa - MB), (void *)fdt_paddr, MB);

   /* Kernel physical address must be aligned to L1 level big pages(2/4 MB) */
   ASSERT(!((ulong)kernel_pa & (L1_PAGE_SIZE - 1)));

   /* Identity map the first 8 MB */
   create_early_page_table(pgd, base_pa, base_pa, EARLY_MAP_SIZE, false);

   /* Map the first 8 MB also at BASE_VA */
   create_early_page_table(pgd, BASE_VA, base_pa, EARLY_MAP_SIZE, false);

   if (!KRN32_LIN_VADDR) {
      /* Map the first 8 MB AT KERNEL_BASE_VA*/
      create_early_page_table(pgd,
                              KERNEL_BASE_VA,
                              base_pa,
                              EARLY_MAP_SIZE,
                              false);
   }

   /* Flush entire TLB */
   invalidate_page_hw(0);

   /* Return physical addr of FDT */
   return (void *)(kernel_pa - MB);
}

void set_pages_pat_wc(pdir_t *pdir, void *vaddr, size_t size)
{
   NOT_IMPLEMENTED();
}

void set_pages_io(pdir_t *pdir, void *vaddr, size_t size)
{
   NOT_IMPLEMENTED();
}

void early_init_paging(void)
{
   NOT_IMPLEMENTED();
}

void init_hi_vmem_heap(void)
{
   NOT_IMPLEMENTED();
}

void *failsafe_map_framebuffer(ulong paddr, ulong size)
{
   NOT_IMPLEMENTED();
}

int
virtual_read_unsafe(pdir_t *pdir, void *extern_va, void *dest, size_t len)
{
   NOT_IMPLEMENTED();
}

int
virtual_write_unsafe(pdir_t *pdir, void *extern_va, void *src, size_t len)
{
   NOT_IMPLEMENTED();
}
