
#pragma once

#include <paging.h>

size_t stackwalk32(void **frames, size_t count,
                   void *ebp, page_directory_t *pdir);

void dump_stacktrace();

int debug_count_used_pdir_entries(page_directory_t *pdir);
void debug_dump_used_pdir_entries(page_directory_t *pdir);


