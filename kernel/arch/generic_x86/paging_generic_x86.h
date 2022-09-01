/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>
#include <tilck_gen_headers/mod_fb.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>

/*
 * When this flag is set in the 'avail' bits in page_t, in means that the page
 * is writeable even if it marked as read-only and that, on a write attempt
 * the page has to be copied (copy-on-write).
 */
#define PAGE_COW_ORIG_RW                       (1 << 0)

/*
 * When this flag is set in the 'avail' bits in page_t, it means that the page
 * is shared and, therefore, can never become a CoW page.
 */
#define PAGE_SHARED                            (1 << 1)


/* ---------------------------------------------- */

extern char zero_page[PAGE_SIZE] ALIGNED_AT(PAGE_SIZE);

extern u32 *pageframes_refcount;
extern ulong phys_mem_lim;
extern struct kmalloc_heap *hi_vmem_heap;

static ALWAYS_INLINE u32 __pf_ref_count_inc(u32 paddr)
{
   return ++pageframes_refcount[paddr >> PAGE_SHIFT];
}

static ALWAYS_INLINE u32 __pf_ref_count_dec(u32 paddr)
{
   ASSERT(pageframes_refcount[paddr >> PAGE_SHIFT] > 0);
   return --pageframes_refcount[paddr >> PAGE_SHIFT];
}

static ALWAYS_INLINE u32 pf_ref_count_inc(u32 paddr)
{
   if (UNLIKELY(paddr >= phys_mem_lim))
      return 0;

   return __pf_ref_count_inc(paddr);
}

static ALWAYS_INLINE u32 pf_ref_count_dec(u32 paddr)
{
   if (UNLIKELY(paddr >= phys_mem_lim))
      return 0;

   return __pf_ref_count_dec(paddr);
}

static ALWAYS_INLINE u32 pf_ref_count_get(u32 paddr)
{
   if (UNLIKELY(paddr >= phys_mem_lim))
      return 0;

   return pageframes_refcount[paddr >> PAGE_SHIFT];
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
