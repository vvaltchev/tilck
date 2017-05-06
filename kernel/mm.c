
// Arch independent memory management stuff

#include <common_defs.h>
#include <paging.h>

page_directory_t *kernel_page_dir = NULL;
page_directory_t *curr_page_dir = NULL;
void *page_size_buf = NULL;
u16 *pageframes_refcount = NULL;
