
#pragma once

#include <commonDefs.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE ((uptr)1 << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define OFFSET_IN_PAGE_MASK (PAGE_SIZE - 1)

#define KERNEL_BASE_VADDR ((uptr) 0xC0000000UL)

void init_pageframe_allocator();
void *alloc_pageframe();
void free_pageframe(void *address);
int get_free_pageframes_count();

#ifdef __i386__
#define PAGE_DIR_SIZE (2 * PAGE_SIZE + 4)
#endif


// Forward-declaring page_directory_t
typedef struct page_directory_t page_directory_t;

void init_paging();

void initialize_page_directory(page_directory_t *pdir, uptr paddr, bool us);

void map_page(page_directory_t *pdir,
              uptr vaddr,
              uptr paddr,
              bool us,
              bool rw);

bool is_mapped(page_directory_t *pdir, uptr vaddr);
void unmap_page(page_directory_t *pdir, uptr vaddr);

void *get_mapping(page_directory_t *pdir, uptr vaddr);

page_directory_t *pdir_clone(page_directory_t *pdir);

static inline void
map_pages(page_directory_t *pdir,
          uptr vaddr,
          uptr paddr,
          int pageCount,
          bool us,
          bool rw)
{
   for (int i = 0; i < pageCount; i++) {
      map_page(pdir, vaddr + (i << PAGE_SHIFT), paddr + (i << PAGE_SHIFT), us, rw);
   }
}

extern page_directory_t *kernel_page_dir;
extern page_directory_t *curr_page_dir;

void set_page_directory(page_directory_t *dir);

static ALWAYS_INLINE page_directory_t *get_curr_page_dir()
{
   return curr_page_dir;
}

static ALWAYS_INLINE page_directory_t *get_kernel_page_dir()
{
   return kernel_page_dir;
}
