/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/page_size.h>
#include <tilck/kernel/hal_types.h>

#if defined(__i386__) || defined(__riscv)
   #define PAGE_DIR_SIZE (PAGE_SIZE)
#endif

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
#define PAGING_FL_DO_ALLOC                                (1 << 4)
#define PAGING_FL_ZERO_PG                                 (1 << 5)

/* Combo values */
#define PAGING_FL_RWUS               (PAGING_FL_RW | PAGING_FL_US)

/*
 * These MACROs convert addresses to/from the linear mapping at BASE_VA to the
 * physical address space.
 */
#if defined(__i386__) || defined(__x86_64__)

   #define PA_TO_LIN_VA(pa) ((void *) ((ulong)(pa) + BASE_VA))
   #define LIN_VA_TO_PA(va) ((ulong)(va) - BASE_VA)

#elif defined(__riscv)

   extern ulong linear_va_pa_offset;
   #define PA_TO_LIN_VA(pa) ((void *) ((ulong)(pa) + linear_va_pa_offset))
   #define LIN_VA_TO_PA(va) ((ulong)(va) - linear_va_pa_offset)

#endif
/*
 * These MACROs convert addresses to/from the kernel base virtual mapping to
 * the physical address space. When KRN32_LIN_VADDR is enabled, KERNEL_BASE_VA
 * is the same as BASE_VA, so the following macros are identical to the ones
 * above. When KRN32_LIN_VADDR is disabled, KERNEL_BASE_VA will be != BASE_VA.
 *
 * In the 64-bit case, the kernel will always be mapped at a non linear, as if
 * KRN32_LIN_VADDR were always disabled. Indeed, its value is ignored in the 64
 * bit case.
 */
#if defined(__i386__) || defined(__x86_64__)

   #define PA_TO_KERNEL_VA(pa) ((void *) ((ulong)(pa) + KERNEL_BASE_VA))
   #define KERNEL_VA_TO_PA(va) ((ulong)(va) - KERNEL_BASE_VA)

#elif defined(__riscv)

   extern ulong kernel_va_pa_offset;
   #define PA_TO_KERNEL_VA(pa) ((void *) ((ulong)(pa) + kernel_va_pa_offset))
   #define KERNEL_VA_TO_PA(va) ((ulong)(va) - kernel_va_pa_offset)

#endif

extern char page_size_buf[PAGE_SIZE] ALIGNED_AT(PAGE_SIZE);
extern char zero_page[PAGE_SIZE] ALIGNED_AT(PAGE_SIZE);

void early_init_paging();
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

void init_paging(void);
bool is_mapped(pdir_t *pdir, void *vaddr);
bool is_rw_mapped(pdir_t *pdir, void *vaddrp);
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

NODISCARD static ALWAYS_INLINE int
map_kernel_page(void *vaddr, ulong paddr, u32 pg_flags)
{
   extern pdir_t *__kernel_pdir;
   return map_page(__kernel_pdir, vaddr, paddr, pg_flags);
}

static ALWAYS_INLINE void
unmap_kernel_page(void *vaddr, bool do_free)
{
   extern pdir_t *__kernel_pdir;
   unmap_page(__kernel_pdir, vaddr, do_free);
}

NODISCARD static ALWAYS_INLINE size_t
map_kernel_pages(void *vaddr,
                 ulong paddr,
                 size_t page_count,
                 u32 pg_flags)
{
   extern pdir_t *__kernel_pdir;
   return map_pages(__kernel_pdir, vaddr, paddr, page_count, pg_flags);
}

static ALWAYS_INLINE void
unmap_kernel_pages(void *vaddr, size_t count, bool do_free)
{
   extern pdir_t *__kernel_pdir;
   unmap_pages(__kernel_pdir, vaddr, count, do_free);
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

/* Returns true if the hi_vmem_heap has been initialized */
bool hi_vmem_avail(void);

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

