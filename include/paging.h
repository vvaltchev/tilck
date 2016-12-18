
#pragma once

#include <commonDefs.h>

/*
 * Our simple pageframe allocator supports only 128 MB of RAW,
 * for now. Supporting more than that requires a much more complex
 * implementation in order to keep its performance good.
 */
#define MAX_MEM_SIZE_IN_MB 128

#define PAGE_SHIFT 12
#define PAGE_SIZE ((uptr)1 << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define OFFSET_IN_PAGE_MASK (PAGE_SIZE - 1)

#ifdef __i386__
#define PAGE_DIR_SIZE (2 * PAGE_SIZE + 4)
#endif

#define KERNEL_BASE_VADDR ((uptr) 0xC0000000UL)

void init_pageframe_allocator();
uptr alloc_pageframe();
void free_pageframe(uptr address);
int get_free_pageframes_count();


/*
 * For the moment, we don't know the total amount of RAM present
 * in the system, so we just assume it to be MAX_MEM_SIZE_IN_MB.
 * In the future, we'll fetch that information during the boot
 * stage and return a real value.
 */
static ALWAYS_INLINE int get_amount_of_physical_memory_in_mb()
{
   return MAX_MEM_SIZE_IN_MB;
}


// Forward-declaring page_directory_t
typedef struct page_directory_t page_directory_t;

void init_paging();

void initialize_page_directory(page_directory_t *pdir, uptr paddr, bool us);

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
