
#pragma once

#include <common_defs.h>
#include <paging.h>

#define PG_PRESENT_BIT_POS      0
#define PG_RW_BIT_POS           1
#define PG_US_BIT_POS           2
#define PG_WT_BIT_POS           3
#define PG_CD_BIT_POS           4
#define PG_ACC_BIT_POS          5
#define PG_DIRTY_BIT_POS        6 // page_t only
#define PG_4MB_BIT_POS          7 // page_dir_entry_t only
#define PG_GLOBAL_BIT_POS       8 // page_t only
#define PG_CUSTOM_B0_POS        9
#define PG_CUSTOM_B1_POS       10
#define PG_CUSTOM_B2_POS       11


#define PG_PRESENT_BIT (1 << PG_PRESENT_BIT_POS)
#define PG_RW_BIT      (1 << PG_RW_BIT_POS)
#define PG_US_BIT      (1 << PG_US_BIT_POS)
#define PG_WT_BIT      (1 << PG_WT_BIT_POS)
#define PG_CD_BIT      (1 << PG_CD_BIT_POS)
#define PG_ACC_BIT     (1 << PG_ACC_BIT_POS)
#define PG_DIRTY_BIT   (1 << PG_DIRTY_BIT_POS)   // page_t only
#define PG_4MB_BIT     (1 << PG_4MB_BIT_POS)     // page_dir_entry_t only
#define PG_GLOBAL_BIT  (1 << PG_GLOBAL_BIT_POS)  // page_t only
#define PG_CUSTOM_B0   (1 << PG_CUSTOM_B0_POS)
#define PG_CUSTOM_B1   (1 << PG_CUSTOM_B1_POS)
#define PG_CUSTOM_B2   (1 << PG_CUSTOM_B2_POS)


// A page table entry
typedef struct {

   union {

      struct {
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
      };

      u32 raw;
   };

} page_t;


// A page table
typedef struct {

   page_t pages[1024];

} page_table_t;


// A page directory entry
typedef struct {

   union {

      struct {
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
      };

      u32 raw;
   };

} page_dir_entry_t;


// A page directory
struct page_directory_t {

   page_dir_entry_t entries[1024];  // actual entries used by the CPU
   page_table_t *page_tables[1024]; // pointers to the tables (VAs)
   uptr paddr;                      // physical address of this page directory
};

STATIC_ASSERT(sizeof(page_directory_t) == PAGE_DIR_SIZE);
