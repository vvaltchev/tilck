
#pragma once

#include <tilck/common/basic_defs.h>

#ifdef __i386__
#define PAGE_DIR_SIZE (PAGE_SIZE)
#endif

#define PAGE_SHIFT 12
#define PAGE_SIZE ((uptr)1 << PAGE_SHIFT)
#define OFFSET_IN_PAGE_MASK (PAGE_SIZE - 1)
#define PAGE_MASK (~OFFSET_IN_PAGE_MASK)
#define IS_PAGE_ALIGNED(x) (!((uptr)x & OFFSET_IN_PAGE_MASK))

#define INVALID_PADDR ((uptr)-1)

/*
 * These MACROs can be used for the linear mapping region in the kernel space.
 */

#define KERNEL_PA_TO_VA(pa) ((void *) ((uptr)(pa) + KERNEL_BASE_VA))
#define KERNEL_VA_TO_PA(va) ((uptr)(va) - KERNEL_BASE_VA)

typedef struct page_directory_t page_directory_t;

void init_paging();
bool handle_potential_cow(void *r);

NODISCARD int
map_page(page_directory_t *pdir, void *vaddr, uptr paddr, bool us, bool rw);

NODISCARD int
map_page_int(page_directory_t *pdir, void *vaddr, uptr paddr, u32 flags);

NODISCARD int
map_zero_page(page_directory_t *pdir, void *vaddrp, bool us, bool rw);

NODISCARD int
map_pages(page_directory_t *pdir,
          void *vaddr,
          uptr paddr,
          int page_count,
          bool big_pages_allowed,
          bool us,
          bool rw);

NODISCARD int
map_pages_int(page_directory_t *pdir,
              void *vaddr,
              uptr paddr,
              int page_count,
              bool big_pages_allowed,
              u32 flags);

NODISCARD int
map_zero_pages(page_directory_t *pdir,
               void *vaddrp,
               int page_count,
               bool us,
               bool rw);

bool is_mapped(page_directory_t *pdir, void *vaddr);
void unmap_page(page_directory_t *pdir, void *vaddr, bool free_pageframe);
uptr get_mapping(page_directory_t *pdir, void *vaddr);
page_directory_t *pdir_clone(page_directory_t *pdir);
page_directory_t *pdir_deep_clone(page_directory_t *pdir);
void pdir_destroy(page_directory_t *pdir);

// Temporary function, until get/set page flags is made available.
void set_page_rw(page_directory_t *pdir, void *vaddr, bool rw);

static inline void
unmap_pages(page_directory_t *pdir,
            void *vaddr,
            int page_count,
            bool free_pageframes)
{
   for (int i = 0; i < page_count; i++) {
      unmap_page(pdir, (char *)vaddr + (i << PAGE_SHIFT), free_pageframes);
   }
}

extern page_directory_t *kernel_page_dir;
extern page_directory_t *curr_page_dir;

void set_page_directory(page_directory_t *dir);
void init_paging_cow(void);

static ALWAYS_INLINE page_directory_t *get_curr_pdir()
{
   return curr_page_dir;
}

static ALWAYS_INLINE page_directory_t *get_kernel_pdir()
{
   return kernel_page_dir;
}

void map_framebuffer(uptr paddr, uptr vaddr, uptr size);
void set_pages_pat_wc(page_directory_t *pdir, void *vaddr, size_t size);
