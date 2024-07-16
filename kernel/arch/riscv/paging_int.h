/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/paging.h>

/*
 * PTE format:
 * | XLEN-1  10 | 9             8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 *       PFN      reserved for SW   D   A   G   U   X   W   R   V
 */

#define _PAGE_ACCESSED_OFFSET 6
#define _PAGE_PFN_SHIFT 10

#define _PAGE_PRESENT   (1 << 0)
#define _PAGE_READ      (1 << 1)    /* Readable */
#define _PAGE_WRITE     (1 << 2)    /* Writable */
#define _PAGE_EXEC      (1 << 3)    /* Executable */
#define _PAGE_USER      (1 << 4)    /* User */
#define _PAGE_GLOBAL    (1 << 5)    /* Global */
#define _PAGE_ACCESSED  (1 << 6)    /* Set by hardware on any access */
#define _PAGE_DIRTY     (1 << 7)    /* Set by hardware on any write */
#define _PAGE_SOFT0     (1 << 8)    /* Reserved for software */
#define _PAGE_SOFT1     (1 << 9)    /* Reserved for software */

/* Custom bits defined by the specific vendor */
#define _PAGE_MTMASK  (riscv_cpu_features.page_mtmask)
#define _PAGE_CB      (riscv_cpu_features.page_cb) /* Cacheble & bufferable */
#define _PAGE_WT      (riscv_cpu_features.page_wt) /* Write through */
#define _PAGE_IO      (riscv_cpu_features.page_io) /* Strongly-ordered,
                                                     Non-cacheable,
                                                     Non-bufferable */

#define _PAGE_PROT_NONE _PAGE_READ

#define _PAGE_BASE   (_PAGE_PRESENT    \
                     | _PAGE_READ      \
                     | _PAGE_EXEC      \
                     | _PAGE_ACCESSED  \
                     | _PAGE_DIRTY     \
                     | _PAGE_CB)

#define _PAGE_LEAF (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)

/*
 * When this flag is set in the 'avail' bits in page_t, in means that the page
 * is writeable even if it marked as read-only and that, on a write attempt
 * the page has to be copied (copy-on-write).
 */
#define PAGE_COW_ORIG_RW                       _PAGE_SOFT0

/*
 * When this flag is set in the 'avail' bits in page_t, it means that the page
 * is shared and, therefore, can never become a CoW page.
 */
#define PAGE_SHARED                            _PAGE_SOFT1

#define PAGE_TABLE      (_PAGE_PRESENT)


#if __riscv_xlen == 32

/* Size of region mapped by a page global directory */
#define PGDIR_SHIFT     22
#define PGDIR_SIZE      (1UL << PGDIR_SHIFT)

#define L0_PAGE_SHIFT       PAGE_SHIFT
#define L1_PAGE_SHIFT       PGDIR_SHIFT
#define L1_PAGE_SIZE        PGDIR_SIZE
#define RV_PAGE_LEVEL        1

#else

#define PGDIR_SHIFT     30
/* Size of region mapped by a page global directory */
#define PGDIR_SIZE      (1UL << PGDIR_SHIFT)
#define PMD_SHIFT       21
/* Size of region mapped by a page middle directory */
#define PMD_SIZE        (1UL << PMD_SHIFT)

#define L0_PAGE_SHIFT       PAGE_SHIFT
#define L1_PAGE_SHIFT       PMD_SHIFT
#define L1_PAGE_SIZE        PMD_SIZE
#define L2_PAGE_SHIFT       PGDIR_SHIFT
#define L2_PAGE_SIZE        PGDIR_SIZE
#define RV_PAGE_LEVEL        2

#endif

/* helper macros */
/* Number of entries in the page table */
#define PTRS_PER_PT    (PAGE_DIR_SIZE / sizeof(ulong))

#define PFN(x) ((x) >> PAGE_SHIFT)

/* Extract the each level page table indices from a virtual address */
#define PTE_SHIFT(level)   (PAGE_SHIFT+(9*(level)))
#define PTE_INDEX(level, vaddr)  ((((ulong)(vaddr)) >>   \
                                  PTE_SHIFT(level)) & (PTRS_PER_PT - 1))

#define MAKE_BIG_PAGE(paddr) (_PAGE_BASE | _PAGE_WRITE | _PAGE_GLOBAL \
                        | (PFN(paddr) << _PAGE_PFN_SHIFT))

#define BASE_VADDR_PD_IDX                (USERMODE_VADDR_END >> PGDIR_SHIFT)

#define EARLY_MAP_SIZE  (8 * MB)

// A page table entry
/*
 * PTE format:
 * | XLEN-1  10 | 9             8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 *       PFN      reserved for SW   D   A   G   U   X   W   R   V
 */
union riscv_page {

   struct {
      ulong present : 1;
      ulong rd : 1;
      ulong wr : 1;
      ulong ex : 1;
      ulong usr : 1;
      ulong global : 1;
      ulong access : 1;
      ulong dirty : 1;
      ulong reserved : 2;
      ulong pfn : (__riscv_xlen - 10);
   };

   ulong raw;
};

// A page table
struct riscv_page_table {
   union riscv_page entries[PTRS_PER_PT];
};

STATIC_ASSERT(sizeof(struct riscv_page_table) == PAGE_DIR_SIZE);

void map_big_page_int(pdir_t *pdir,
                      void *vaddr,
                      ulong paddr,
                      u32 flags);

void set_pages_io(pdir_t *pdir, void *vaddr, size_t size);
