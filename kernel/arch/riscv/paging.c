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

void *init_early_mapping(ulong fdt_paddr)
{
   NOT_IMPLEMENTED();
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
