/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal_types.h>

#ifdef __i386__
   #define PAGE_DIR_SIZE (PAGE_SIZE)
#endif

#define PAGE_SHIFT                                              12
#define PAGE_SIZE                         ((ulong)1 << PAGE_SHIFT)
#define OFFSET_IN_PAGE_MASK                        (PAGE_SIZE - 1)
#define PAGE_MASK                           (~OFFSET_IN_PAGE_MASK)
#define IS_PAGE_ALIGNED(x)     (!((ulong)x & OFFSET_IN_PAGE_MASK))
#define IS_PTR_ALIGNED(x)        (!((ulong)x & (sizeof(ulong)-1)))

#define INVALID_PADDR                                  ((ulong)-1)

/* Paging flags (pg_flags) */
#define PAGING_FL_RW                                      (1 << 0)
#define PAGING_FL_US                                      (1 << 1)
#define PAGING_FL_BIG_PAGES_ALLOWED                       (1 << 2)
#define PAGING_FL_SHARED                                  (1 << 3)

/* Combo values */
#define PAGING_FL_RWUS               (PAGING_FL_RW | PAGING_FL_US)

/*
 * These MACROs can be used for the linear mapping region in the kernel space.
 */

#define KERNEL_PA_TO_VA(pa) ((void *) ((ulong)(pa) + KERNEL_BASE_VA))
#define KERNEL_VA_TO_PA(va) ((ulong)(va) - KERNEL_BASE_VA)

extern char page_size_buf[PAGE_SIZE];
extern char zero_page[PAGE_SIZE];

void init_paging();
bool handle_potential_cow(void *r);

/*
 * Map a pageframe at `paddr` at the virtual address `vaddr` in the page
 * directory `pdir`, using the arch-independent `pg_flags`. This last param
 * is made by ORing the flags defined above such as PAGING_FL_RO etc.
 */

NODISCARD int
map_page(pdir_t *pdir, void *vaddr, ulong paddr, u32 pg_flags);

NODISCARD int
map_page_int(pdir_t *pdir, void *vaddr, ulong paddr, u32 hw_flags);

NODISCARD int
map_zero_page(pdir_t *pdir, void *vaddrp, u32 pg_flags);

NODISCARD size_t
map_pages(pdir_t *pdir,
          void *vaddr,
          ulong paddr,
          size_t page_count,
          u32 pg_flags);

NODISCARD size_t
map_pages_int(pdir_t *pdir,
              void *vaddr,
              ulong paddr,
              size_t page_count,
              bool big_pages_allowed,
              u32 hw_flags);

NODISCARD size_t
map_zero_pages(pdir_t *pdir,
               void *vaddrp,
               size_t page_count,
               u32 pg_flags);

void init_paging_cow(void);
bool is_mapped(pdir_t *pdir, void *vaddr);
void unmap_page(pdir_t *pdir, void *vaddr, bool do_free);
int unmap_page_permissive(pdir_t *pdir, void *vaddrp, bool do_free);
void unmap_pages(pdir_t *pdir, void *vaddr, size_t count, bool do_free);
size_t unmap_pages_permissive(pdir_t *pd, void *va, size_t count, bool do_free);
ulong get_mapping(pdir_t *pdir, void *vaddr);
int get_mapping2(pdir_t *pdir, void *vaddrp, ulong *pa_ref);
pdir_t *pdir_clone(pdir_t *pdir);
pdir_t *pdir_deep_clone(pdir_t *pdir);
void pdir_destroy(pdir_t *pdir);
void invalidate_page(ulong vaddr);
void set_page_rw(pdir_t *pdir, void *vaddr, bool rw);
void retain_pageframes_mapped_at(pdir_t *pdir, void *vaddr, size_t len);
void release_pageframes_mapped_at(pdir_t *pdir, void *vaddr, size_t len);

static ALWAYS_INLINE pdir_t *get_kernel_pdir(void)
{
   extern pdir_t *__kernel_pdir;
   return __kernel_pdir;
}

void *
map_framebuffer(pdir_t *pdir,
                ulong paddr,
                ulong vaddr,
                ulong size,
                bool user_mmap);

void set_pages_pat_wc(pdir_t *pdir, void *vaddr, size_t size);

/*
 * Reserve anywhere in the hi virtual mem area (from LINEAR_MAPPING_END to
 * +4 GB on 32-bit systems) a block. Note: no actual mapping is done here,
 * just virtual memory is reserved in order to avoid conflicts between multiple
 * sub-systems trying reserve some virtual space here.
 *
 * Callers are expected to do the actual mapping of the virtual memory area
 * returned (if not NULL) to an actual physical address.
 */
void *hi_vmem_reserve(size_t size);

/*
 * Counter-part of hi_vmem_reserve().
 *
 * As above: this function *does not* do any kind of ummap. It's all up to the
 * callers. The function just releases the allocated block in the virtual space.
 */
void hi_vmem_release(void *ptr, size_t size);

int virtual_read(pdir_t *pdir, void *extern_va, void *dest, size_t len);
int virtual_write(pdir_t *pdir, void *extern_va, void *src, size_t len);

static ALWAYS_INLINE void
debug_checked_virtual_read(pdir_t *pdir, void *ext_va, void *dest, size_t len)
{
   DEBUG_ONLY_UNSAFE(int rc =)
      virtual_read(pdir, ext_va, dest, len);

   ASSERT((size_t)rc == len);
}

static ALWAYS_INLINE void
debug_checked_virtual_write(pdir_t *pdir, void *ext_va, void *src, size_t len)
{
   DEBUG_ONLY_UNSAFE(int rc =)
      virtual_write(pdir, ext_va, src, len);

   ASSERT((size_t)rc == len);
}

