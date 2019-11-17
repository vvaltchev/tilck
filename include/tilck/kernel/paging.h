/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal_types.h>

#ifdef __i386__
#define PAGE_DIR_SIZE (PAGE_SIZE)
#endif

#define PAGE_SHIFT                                            12
#define PAGE_SIZE                        ((uptr)1 << PAGE_SHIFT)
#define OFFSET_IN_PAGE_MASK                      (PAGE_SIZE - 1)
#define PAGE_MASK                         (~OFFSET_IN_PAGE_MASK)
#define IS_PAGE_ALIGNED(x)    (!((uptr)x & OFFSET_IN_PAGE_MASK))
#define IS_PTR_ALIGNED(x)        (!((uptr)x & (sizeof(uptr)-1)))

#define INVALID_PADDR                                 ((uptr)-1)

/*
 * These MACROs can be used for the linear mapping region in the kernel space.
 */

#define KERNEL_PA_TO_VA(pa) ((void *) ((uptr)(pa) + KERNEL_BASE_VA))
#define KERNEL_VA_TO_PA(va) ((uptr)(va) - KERNEL_BASE_VA)

void init_paging();
bool handle_potential_cow(void *r);

NODISCARD int
map_page(pdir_t *pdir, void *vaddr, uptr paddr, bool us, bool rw);

NODISCARD int
map_page_int(pdir_t *pdir, void *vaddr, uptr paddr, u32 flags);

NODISCARD int
map_zero_page(pdir_t *pdir, void *vaddrp, bool us, bool rw);

NODISCARD size_t
map_pages(pdir_t *pdir,
          void *vaddr,
          uptr paddr,
          size_t page_count,
          bool big_pages_allowed,
          bool us,
          bool rw);

NODISCARD size_t
map_pages_int(pdir_t *pdir,
              void *vaddr,
              uptr paddr,
              size_t page_count,
              bool big_pages_allowed,
              u32 flags);

NODISCARD size_t
map_zero_pages(pdir_t *pdir,
               void *vaddrp,
               size_t page_count,
               bool us,
               bool rw);

void init_paging_cow(void);
bool is_mapped(pdir_t *pdir, void *vaddr);
void unmap_page(pdir_t *pdir, void *vaddr, bool do_free);
int unmap_page_permissive(pdir_t *pdir, void *vaddrp, bool do_free);
void unmap_pages(pdir_t *pdir, void *vaddr, size_t count, bool do_free);
size_t unmap_pages_permissive(pdir_t *pd, void *va, size_t count, bool do_free);
uptr get_mapping(pdir_t *pdir, void *vaddr);
pdir_t *pdir_clone(pdir_t *pdir);
pdir_t *pdir_deep_clone(pdir_t *pdir);
void pdir_destroy(pdir_t *pdir);
void invalidate_page(uptr vaddr);

// Temporary function, until get/set page flags is made available.
void set_page_rw(pdir_t *pdir, void *vaddr, bool rw);

extern pdir_t *kernel_page_dir;
extern char page_size_buf[PAGE_SIZE];
extern char zero_page[PAGE_SIZE];

static ALWAYS_INLINE pdir_t *get_kernel_pdir(void)
{
   return kernel_page_dir;
}

void *map_framebuffer(uptr paddr, uptr vaddr, uptr size, bool user_mmap);
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
