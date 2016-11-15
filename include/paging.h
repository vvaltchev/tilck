
#pragma once

#include <commonDefs.h>

#define PAGE_SIZE (4096)
#define KERNEL_BASE_VADDR ((uintptr_t) 0xC0000000UL)

void init_physical_page_allocator();
void *alloc_phys_page();
void free_phys_page(void *address);
int get_free_physical_pages_count();

// Forward-declaring page_directory_t
typedef struct page_directory_t page_directory_t;

void init_paging();
page_directory_t *get_curr_page_dir();
page_directory_t *get_kernel_page_dir();

void map_page(page_directory_t *pdir,
              uintptr_t vaddr,
	           uintptr_t paddr,
              bool us,
              bool rw);

bool is_mapped(page_directory_t *pdir, uintptr_t vaddr);
bool unmap_page(page_directory_t *pdir, uintptr_t vaddr);

void *get_mapping(page_directory_t *pdir, uintptr_t vaddr);

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
