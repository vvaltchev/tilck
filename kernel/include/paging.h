
#pragma once

#include <common_defs.h>

// Max memory size supported by the pageframe allocator.
#define MAX_MEM_SIZE_IN_MB 1024

#define PAGE_SHIFT 12
#define PAGE_SIZE ((uptr)1 << PAGE_SHIFT)
#define OFFSET_IN_PAGE_MASK (PAGE_SIZE - 1)
#define PAGE_MASK (~OFFSET_IN_PAGE_MASK)

#ifdef __i386__
#define PAGE_DIR_SIZE (2 * PAGE_SIZE + 4)
#endif

/* Internal defines specific for the pageframe allocator */

#define INITIAL_MB_RESERVED 2
#define MB_RESERVED_FOR_PAGING 2

/* Public interface of the pageframe allocator */

void init_pageframe_allocator(void);
uptr alloc_pageframe(void);
uptr alloc_32_pageframes(void);
uptr alloc_32_pageframes_aligned(void);
void free_32_pageframes(uptr paddr);
void free_pageframe(uptr address);
int get_free_pageframes_count(void);
int get_total_pageframes_count(void);
void init_paging_pageframe_allocator(void);


extern u32 memsize_in_mb;

static ALWAYS_INLINE int get_amount_of_physical_memory_in_mb()
{
   return memsize_in_mb;
}

/* Paging-related stuff */

// Forward-declaring page_directory_t
typedef struct page_directory_t page_directory_t;

void init_paging();

void map_page(page_directory_t *pdir,
              void *vaddr,
              uptr paddr,
              bool us,
              bool rw);

bool is_mapped(page_directory_t *pdir, void *vaddr);
void unmap_page(page_directory_t *pdir, void *vaddr);

uptr get_mapping(page_directory_t *pdir, void *vaddr);

page_directory_t *pdir_clone(page_directory_t *pdir);
void pdir_destroy(page_directory_t *pdir);

// Temporary function, untit get/set page flags is made available.
void set_page_rw(page_directory_t *pdir, void *vaddr, bool rw);

static inline void
map_pages(page_directory_t *pdir,
          void *vaddr,
          uptr paddr,
          int pageCount,
          bool us,
          bool rw)
{
   for (int i = 0; i < pageCount; i++) {
      map_page(pdir,
               (u8 *)vaddr + (i << PAGE_SHIFT),
               paddr + (i << PAGE_SHIFT),
               us,
               rw);
   }
}

static inline void
unmap_pages(page_directory_t *pdir, void *vaddr, int pageCount)
{
   for (int i = 0; i < pageCount; i++) {
      unmap_page(pdir, (u8 *)vaddr + (i << PAGE_SHIFT));
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
