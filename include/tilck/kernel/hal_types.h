/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#if defined(__i386__) && !defined(__x86_64__)

   typedef union x86_page page_t;
   typedef struct x86_page_table page_table_t;
   typedef union x86_page_dir_entry page_dir_entry_t;
   typedef struct x86_pdir pdir_t;
   typedef struct x86_regs regs_t;
   typedef struct x86_arch_task_members arch_task_members_t;

#elif defined(__x86_64__)

   typedef void *pdir_t;
   typedef struct x86_64_regs regs_t;
   typedef struct x86_64_arch_task_members arch_task_members_t;

#else

   #error Unsupported architecture.

#endif

enum irq_action {

   IRQ_UNHANDLED = -1,         /* the irq was not handled at all */
   IRQ_FULLY_HANDLED = 0,      /* no more work required */
   IRQ_REQUIRES_BH = 1         /* requires a botton half (tasklet) to run */
};

typedef void (*soft_int_handler_t)(regs_t *);
typedef enum irq_action (*irq_handler_t)(regs_t *);
