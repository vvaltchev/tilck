/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/page_size.h>

#if defined(__i386__) && !defined(__x86_64__)

   typedef union x86_page page_t;
   typedef struct x86_page_table page_table_t;
   typedef union x86_page_dir_entry page_dir_entry_t;
   typedef struct x86_pdir pdir_t;
   typedef struct x86_regs regs_t;
   typedef struct x86_arch_task_members arch_task_members_t;
   typedef struct x86_arch_proc_members arch_proc_members_t;

   #define ARCH_TASK_MEMBERS_SIZE     8
   #define ARCH_TASK_MEMBERS_ALIGN    4

   #define ARCH_PROC_MEMBERS_SIZE    16
   #define ARCH_PROC_MEMBERS_ALIGN    4

#elif defined(__x86_64__)

   typedef void *pdir_t;
   typedef struct x86_64_regs regs_t;
   typedef struct x86_64_arch_task_members arch_task_members_t;
   typedef struct x86_64_arch_proc_members arch_proc_members_t;

   #define ARCH_TASK_MEMBERS_SIZE    16
   #define ARCH_TASK_MEMBERS_ALIGN    8

   #define ARCH_PROC_MEMBERS_SIZE     8
   #define ARCH_PROC_MEMBERS_ALIGN    8

#else

   #error Unsupported architecture.

#endif

enum irq_action {

   IRQ_NOT_HANDLED     = 0,
   IRQ_HANDLED         = 1,
};

typedef void (*soft_int_handler_t)(regs_t *);
typedef enum irq_action (*irq_handler_t)(void *ctx);

STATIC_ASSERT(PAGE_SIZE == (1u << PAGE_SHIFT));
STATIC_ASSERT(
   KERNEL_STACK_PAGES == 1 ||
   KERNEL_STACK_PAGES == 2 ||
   KERNEL_STACK_PAGES == 4
);
