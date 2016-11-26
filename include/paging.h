
#pragma once

#include <commonDefs.h>

#define PAGE_SIZE (4096)
#define KERNEL_BASE_VADDR ((uintptr_t) 0xC0000000UL)

void init_physical_page_allocator();
void *alloc_phys_page();
void free_phys_page(void *address);
int get_free_physical_pages_count();

#ifdef __i386__
#define PAGE_DIR_SIZE (2 * PAGE_SIZE + 4)
#endif


// Forward-declaring page_directory_t
typedef struct page_directory_t page_directory_t;

void init_paging();

void initialize_page_directory(page_directory_t *pdir, uintptr_t paddr, bool us);
void add_kernel_base_mappings(page_directory_t *pdir);

void map_page(page_directory_t *pdir,
              uintptr_t vaddr,
	           uintptr_t paddr,
              bool us,
              bool rw);

bool is_mapped(page_directory_t *pdir, uintptr_t vaddr);
void unmap_page(page_directory_t *pdir, uintptr_t vaddr);

void *get_mapping(page_directory_t *pdir, uintptr_t vaddr);

page_directory_t *pdir_clone(page_directory_t *pdir);

static inline void
map_pages(page_directory_t *pdir,
          uintptr_t vaddr,
          uintptr_t paddr,
          int pageCount,
          bool us,
          bool rw)
{
   for (int i = 0; i < pageCount; i++) {
      map_page(pdir, vaddr + (i << 12), paddr + (i << 12), us, rw);
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