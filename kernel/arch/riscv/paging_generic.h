/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>
#include <tilck_gen_headers/mod_fb.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>

/* ---------------------------------------------- */
#define MEM_LOW (__mem_lower_kb << 10)

extern char zero_page[PAGE_SIZE] ALIGNED_AT(PAGE_SIZE);
extern u32 __mem_lower_kb;

extern u32 *pageframes_refcount;
extern ulong phys_mem_lim;
extern struct kmalloc_heap *hi_vmem_heap;

static ALWAYS_INLINE u32 __pf_ref_count_inc(u32 paddr)
{
   return ++pageframes_refcount[(paddr - MEM_LOW) >> PAGE_SHIFT];
}

static ALWAYS_INLINE u32 __pf_ref_count_dec(u32 paddr)
{
   ASSERT(pageframes_refcount[(paddr - MEM_LOW) >> PAGE_SHIFT] > 0);
   return --pageframes_refcount[(paddr - MEM_LOW) >> PAGE_SHIFT];
}

static ALWAYS_INLINE u32 pf_ref_count_inc(u32 paddr)
{
   if (UNLIKELY((paddr - MEM_LOW) >= phys_mem_lim))
      return 0;

   return __pf_ref_count_inc(paddr);
}

static ALWAYS_INLINE u32 pf_ref_count_dec(u32 paddr)
{
   if (UNLIKELY((paddr - MEM_LOW) >= phys_mem_lim))
      return 0;

   return __pf_ref_count_dec(paddr);
}

static ALWAYS_INLINE u32 pf_ref_count_get(u32 paddr)
{
   if (UNLIKELY((paddr - MEM_LOW) >= phys_mem_lim))
      return 0;

   return pageframes_refcount[(paddr - MEM_LOW) >> PAGE_SHIFT];
}

void init_hi_vmem_heap(void);
void *failsafe_map_framebuffer(ulong paddr, ulong size);
int virtual_read_unsafe(pdir_t *pdir, void *extern_va, void *dest, size_t len);
int virtual_write_unsafe(pdir_t *pdir, void *extern_va, void *src, size_t len);

size_t
map_zero_pages(pdir_t *pdir,
               void *vaddrp,
               size_t page_count,
               u32 pg_flags);

void handle_page_fault_int(regs_t *r);
void handle_page_fault(regs_t *r);
