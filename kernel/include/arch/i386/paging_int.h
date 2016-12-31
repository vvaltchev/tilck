
#pragma once

#include <common_defs.h>
#include <paging.h>

// A page table entry
typedef struct {

   u32 present : 1;
   u32 rw : 1;        // read only = 0, read/write = 1
   u32 us : 1;        // user/supervisor
   u32 wt : 1;        // write-through
   u32 cd : 1;        // cache-disabled
   u32 accessed : 1;
   u32 dirty : 1;
   u32 zero : 1;
   u32 global : 1;
   u32 avail : 3;
   u32 pageAddr : 20; // the first 20 bits of the physical addr.

} page_t;


// A page table
typedef struct {

   page_t pages[1024];

} page_table_t;


// A page directory entry
typedef struct {

   u32 present : 1;
   u32 rw : 1;             // read only = 0, read/write = 1
   u32 us : 1;             // us = 0 -> supervisor only, 1 -> user also
   u32 wt : 1;             // write-through
   u32 cd : 1;             // cache-disabled
   u32 accessed : 1;
   u32 zero : 1;
   u32 psize : 1;          // page size; 0 = 4 KB, 1 = 4 MB
   u32 ignored : 1;
   u32 avail : 3;
   u32 pageTableAddr : 20; // aka, 'page_table_t *'

} page_dir_entry_t;


// A page directory
struct page_directory_t {

   page_dir_entry_t entries[1024];  // actual entries used by the CPU
   page_table_t *page_tables[1024]; // pointers to the tables (VAs)
   uptr paddr;                      // physical address of this page directory
};

STATIC_ASSERT(sizeof(page_directory_t) == PAGE_DIR_SIZE);
