
#pragma once

#include <common_defs.h>
#include <paging.h>

// Max memory size supported by the pageframe allocator.
#define MAX_MEM_SIZE_IN_MB 1024


/* Public interface of the pageframe allocator */

void init_pageframe_allocator(void);
uptr alloc_pageframe(void);
uptr alloc_32_pageframes(void);
uptr alloc_32_pageframes_aligned(void);
void free_32_pageframes(uptr paddr);
void free_pageframe(uptr address);
uptr alloc_8_pageframes(void);
void free_8_pageframes(uptr paddr);
void init_paging_pageframe_allocator(void);
bool is_allocated_pageframe(uptr address);
void mark_pageframes_as_reserved(uptr paddr, int mb_count);

extern int pageframes_used;

static ALWAYS_INLINE int get_total_pageframes_count(void)
{
   return (memsize_in_mb << 20) >> PAGE_SHIFT;
}

static ALWAYS_INLINE int get_free_pageframes_count(void)
{
   return get_total_pageframes_count() - pageframes_used;
}

static ALWAYS_INLINE int get_used_pageframes_count(void)
{
   return pageframes_used;
}
