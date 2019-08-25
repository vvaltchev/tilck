/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

/* Utils */

#define KB (1024u)
#define MB (1024u * 1024u)

#include <generated_config.h>

/* Constants that have no reason to be changed */
#define KMALLOC_FREE_MEM_POISON_VAL    0xFAABCAFE

/* Special advanced developer-only debug utils */
#define KMUTEX_STATS_ENABLED                    0
#define SLOW_DEBUG_REF_COUNT                    0

/* ------------------------------- */

/*
 * For the moment, keep those defines always defined.
 * Unless qemu is run with -device isa-debug-exit,iobase=0xf4,iosize=0x04
 * debug_qemu_turn_off_machine() won't turn off the VM.
 */

#define DEBUG_QEMU_EXIT_ON_INIT_EXIT   1
#define DEBUG_QEMU_EXIT_ON_PANIC       1

#define KERNEL_STACK_SIZE (4 * KB)

#if defined(TESTING) || defined(KERNEL_TEST)

   extern void *kernel_va;

   /* For the unit tests, we need to override the following defines */
   #undef KERNEL_BASE_VA
   #undef LINEAR_MAPPING_MB

   #define KERNEL_BASE_VA             ((uptr)kernel_va)
   #define LINEAR_MAPPING_MB          (128u)

#endif

#define LINEAR_MAPPING_SIZE        (LINEAR_MAPPING_MB << 20)
#define LINEAR_MAPPING_END         (KERNEL_BASE_VA + LINEAR_MAPPING_SIZE)

#define KMALLOC_MAX_ALIGN          (64 * KB)
#define KMALLOC_MIN_HEAP_SIZE      KMALLOC_MAX_ALIGN

#if !KERNEL_GCOV
   #define KMALLOC_FIRST_HEAP_SIZE    (256 * KB)
   #define KERNEL_MAX_SIZE            (1024 * KB)
   #define SYMTAB_MAX_SIZE            (32 * KB)
   #define STRTAB_MAX_SIZE            (32 * KB)
#else
   #define KMALLOC_FIRST_HEAP_SIZE    (512 * KB)
   #define KERNEL_MAX_SIZE            (2048 * KB)
   #define SYMTAB_MAX_SIZE            (128 * KB)
   #define STRTAB_MAX_SIZE            (128 * KB)
#endif

#define USER_VSDO_LIKE_PAGE_VADDR  (LINEAR_MAPPING_END)

#define MAX_TTYS                                    9
#define TERM_SCROLL_LINES                           5
#define MAX_MOUNTPOINTS                            16
#define MAX_NESTED_INTERRUPTS                      32
#define TTY_INPUT_BS                             1024
#define FBCON_OPT_FUNCS_MIN_FREE_HEAP       (16 * MB)

/*
 * User tasks constants
 *
 * WARNING: some of them are NOT really "configurable" without having to modify
 * a lot of code. For example, USERMODE_VADDR_END cannot be > KERNEL_BASE_VA.
 * Also, making *_COPYBUF_SIZE != 2^PAGE_SIZE will cause a waste of memory.
 * MAX_PID can be set to be less than 32K, but not bigger than 65535 because
 * it has to fit in a 16-bit integer.
 */

#define RAMDISK_SECTOR                   2048 /* do not modify!         */
#define USERMODE_VADDR_END   (KERNEL_BASE_VA) /* biggest user vaddr + 1 */
#define MAX_BRK                  (0x40000000) /* +1 GB (virtual memory) */
#define USER_MMAP_BEGIN               MAX_BRK /* +1 GB (virtual memory) */
#define USER_MMAP_END            (0x80000000) /* +2 GB (virtual memory) */
#define USERMODE_STACK_MAX ((USERMODE_VADDR_END - 1) & POINTER_ALIGN_MASK)

#define MAX_PID                                             32768
#define MAX_PATH                                              256
#define MAX_HANDLES                                            16
#define USER_ARGS_PAGE_COUNT                                    1
#define IO_COPYBUF_SIZE        (USER_ARGS_PAGE_COUNT * PAGE_SIZE)
#define ARGS_COPYBUF_SIZE      (USER_ARGS_PAGE_COUNT * PAGE_SIZE)
#define USERAPP_MAX_ARGS_COUNT                                 32
#define MAX_SCRIPT_REC                                          2

/* Bootloader-specific constants */

#define SECTOR_SIZE          512
#define BL_BASE_SEG          (BL_BASE_ADDR / 16)
