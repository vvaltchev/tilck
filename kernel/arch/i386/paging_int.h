/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/paging.h>

#define PG_PRESENT_BIT_POS      0u
#define PG_RW_BIT_POS           1u
#define PG_US_BIT_POS           2u
#define PG_WT_BIT_POS           3u
#define PG_CD_BIT_POS           4u
#define PG_ACC_BIT_POS          5u
#define PG_DIRTY_BIT_POS        6u // page_t only
#define PG_PAGE_PAT_BIT_POS     7u // page_t only
#define PG_4MB_BIT_POS          7u // page_dir_entry_t only
#define PG_GLOBAL_BIT_POS       8u // page_t only
#define PG_CUSTOM_B0_POS        9u
#define PG_CUSTOM_B1_POS       10u
#define PG_CUSTOM_B2_POS       11u
#define PG_4MB_PAT_BIT_POS     12u // page_dir_entry_t only with psize = 1


#define PG_PRESENT_BIT  (1u << PG_PRESENT_BIT_POS)
#define PG_RW_BIT       (1u << PG_RW_BIT_POS)
#define PG_US_BIT       (1u << PG_US_BIT_POS)
#define PG_WT_BIT       (1u << PG_WT_BIT_POS)
#define PG_CD_BIT       (1u << PG_CD_BIT_POS)
#define PG_ACC_BIT      (1u << PG_ACC_BIT_POS)
#define PG_DIRTY_BIT    (1u << PG_DIRTY_BIT_POS)    // page_t only
#define PG_PAGE_PAT_BIT (1u << PG_PAGE_PAT_BIT_POS) // page_t only
#define PG_4MB_BIT      (1u << PG_4MB_BIT_POS)      // page_dir_entry_t only
#define PG_GLOBAL_BIT   (1u << PG_GLOBAL_BIT_POS)   // page_t only
#define PG_CUSTOM_B0    (1u << PG_CUSTOM_B0_POS)
#define PG_CUSTOM_B1    (1u << PG_CUSTOM_B1_POS)
#define PG_CUSTOM_B2    (1u << PG_CUSTOM_B2_POS)
#define PG_CUSTOM_BITS  (PG_CUSTOM_B0 | PG_CUSTOM_B1 | PG_CUSTOM_B2)
#define PG_4MB_PAT_BIT  (1u << PG_4MB_PAT_BIT_POS)

#define PAGE_FAULT_FL_PRESENT (1u << 0)
#define PAGE_FAULT_FL_RW      (1u << 1)
#define PAGE_FAULT_FL_US      (1u << 2)

#define PAGE_FAULT_FL_COW (PAGE_FAULT_FL_PRESENT | PAGE_FAULT_FL_RW)
#define BIG_PAGE_SHIFT                                            22
#define BASE_VADDR_PD_IDX                (BASE_VA >> BIG_PAGE_SHIFT)

// A page table entry

union x86_page {

   struct {
      u32 present : 1;
      u32 rw : 1;        // read only = 0, read/write = 1
      u32 us : 1;        // user/supervisor
      u32 wt : 1;        // write-through
      u32 cd : 1;        // cache-disabled
      u32 accessed : 1;
      u32 dirty : 1;
      u32 pat : 1;
      u32 global : 1;
      u32 avail : 3;
      u32 pageAddr : 20; // the first 20 bits of the physical addr.
   };

   u32 raw;
};

// A page table
struct x86_page_table {

   union x86_page pages[1024];
};

// A page directory entry
union x86_page_dir_entry {

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
      u32 ptaddr : 20;        // the paddr of 'page_table_t *'
   };

   struct {
      u32 present : 1;
      u32 rw : 1;             // read only = 0, read/write = 1
      u32 us : 1;             // us = 0 -> supervisor only, 1 -> user also
      u32 wt : 1;             // write-through
      u32 cd : 1;             // cache-disabled
      u32 accessed : 1;
      u32 zero : 1;
      u32 one : 1;            // psize must be = 1 (4 MB)
      u32 ignored : 1;
      u32 avail : 3;
      u32 pat : 1;
      u32 paddr_zero : 9;
      u32 paddr : 10;         // 4-MB pageframe paddr

   } big_4mb_page;

   u32 raw;
};

// A page directory
struct x86_pdir {
   union x86_page_dir_entry entries[1024];
};

STATIC_ASSERT(sizeof(struct x86_pdir) == PAGE_DIR_SIZE);

void map_4mb_page_int(pdir_t *pdir,
                      void *vaddr,
                      ulong paddr,
                      u32 flags);
