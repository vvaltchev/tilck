
#pragma once

#include <commonDefs.h>

#define KERNEL_BASE_VADDR ((uintptr_t) 0xC0000000UL)
#define KERNEL_PADDR_TO_VADDR(paddr) ((typeof(paddr))((uintptr_t)(paddr) + KERNEL_BASE_VADDR))
#define KERNEL_VADDR_TO_PADDR(vaddr) ((typeof(vaddr))((uintptr_t)(vaddr) - KERNEL_BASE_VADDR))

// Forward-declaring page_directory_t
typedef struct page_directory_t page_directory_t;

void init_paging();
page_directory_t *get_curr_page_dir();

void map_page(page_directory_t *pdir,
              uintptr_t vaddr,
	          uintptr_t paddr,
              bool us,
              bool rw);

bool is_mapped(page_directory_t *pdir, uintptr_t vaddr);
bool unmap_page(page_directory_t *pdir, uintptr_t vaddr);

void map_pages(page_directory_t *pdir,
	           uintptr_t vaddr,
	           uintptr_t paddr,
               int pageCount,
               bool us,
               bool rw);

bool kbasic_virtual_alloc(page_directory_t *pdir, uintptr_t vaddr,
                          size_t size, bool us, bool rw);

bool kbasic_virtual_free(page_directory_t *pdir, uintptr_t vaddr, size_t size);
