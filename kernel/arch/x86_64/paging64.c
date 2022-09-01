/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>
#include <tilck_gen_headers/mod_fb.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>

#include "../generic_x86/paging_generic_x86.h"

pdir_t *__kernel_pdir;

void early_init_paging(void)
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

ulong get_mapping(pdir_t *pdir, void *vaddrp)
{
   NOT_IMPLEMENTED();
}

int get_mapping2(pdir_t *pdir, void *vaddrp, ulong *pa_ref)
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

NODISCARD int
map_page(pdir_t *pdir, void *vaddrp, ulong paddr, u32 pg_flags)
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

NODISCARD int
map_zero_page(pdir_t *pdir, void *vaddrp, u32 pg_flags)
{
   NOT_IMPLEMENTED();
}

static inline int
__unmap_page(pdir_t *pdir, void *vaddrp, bool free_pageframe, bool permissive)
{
   NOT_IMPLEMENTED();
}

void
unmap_page(pdir_t *pdir, void *vaddrp, bool free_pageframe)
{
   __unmap_page(pdir, vaddrp, free_pageframe, false);
}

int
unmap_page_permissive(pdir_t *pdir, void *vaddrp, bool free_pageframe)
{
   return __unmap_page(pdir, vaddrp, free_pageframe, true);
}

void
unmap_pages(pdir_t *pdir,
            void *vaddr,
            size_t page_count,
            bool do_free)
{
   for (size_t i = 0; i < page_count; i++) {
      unmap_page(pdir, (char *)vaddr + (i << PAGE_SHIFT), do_free);
   }
}

size_t
unmap_pages_permissive(pdir_t *pdir,
                       void *vaddr,
                       size_t page_count,
                       bool do_free)
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

void set_pages_pat_wc(pdir_t *pdir, void *vaddr, size_t size)
{
   NOT_IMPLEMENTED();
}

bool handle_potential_cow(void *context)
{
   NOT_IMPLEMENTED();
}

void init_hi_vmem_heap(void)
{
   NOT_IMPLEMENTED();
}
