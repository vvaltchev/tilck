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
#include <tilck/kernel/hal.h>

#include "paging_generic.h"
#include "paging_int.h"

extern u32 __mem_lower_kb;
extern u32 __mem_upper_kb;

u32 *pageframes_refcount;
ulong phys_mem_lim;
struct kmalloc_heap *hi_vmem_heap;

void *ioremap(ulong paddr, size_t size)
{
   NOT_IMPLEMENTED();
}

void iounmap(void *vaddr)
{
   NOT_IMPLEMENTED();
}

void retain_pageframes_mapped_at(pdir_t *pdir, void *vaddrp, size_t len)
{
   NOT_IMPLEMENTED();
}

void release_pageframes_mapped_at(pdir_t *pdir, void *vaddrp, size_t len)
{
   NOT_IMPLEMENTED();
}

void invalidate_page(ulong vaddr)
{
   NOT_IMPLEMENTED();
}

void init_paging(void)
{
   NOT_IMPLEMENTED();
}

void *
map_framebuffer(pdir_t *pdir,
                ulong paddr,
                ulong vaddr,
                ulong size,
                bool user_mmap)
{
   NOT_IMPLEMENTED();
}

bool hi_vmem_avail(void)
{
   NOT_IMPLEMENTED();
}

void *hi_vmem_reserve(size_t size)
{
   NOT_IMPLEMENTED();
}

void hi_vmem_release(void *ptr, size_t size)
{
   NOT_IMPLEMENTED();
}

int virtual_read(pdir_t *pdir, void *extern_va, void *dest, size_t len)
{
   NOT_IMPLEMENTED();
}

int virtual_write(pdir_t *pdir, void *extern_va, void *src, size_t len)
{
   NOT_IMPLEMENTED();
}

NODISCARD size_t
map_zero_pages(pdir_t *pdir,
               void *vaddrp,
               size_t page_count,
               u32 pg_flags)
{
   NOT_IMPLEMENTED();
}

void handle_page_fault(regs_t *r)
{
   NOT_IMPLEMENTED();
}
