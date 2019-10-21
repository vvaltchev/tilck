/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#if defined(__i386__) && !defined(__x86_64__)

   typedef union x86_page page_t;
   typedef struct x86_page_table page_table_t;
   typedef union x86_page_dir_entry page_dir_entry_t;
   typedef struct x86_pdir pdir_t;

#elif defined(__x86_64__)

   typedef void *pdir_t;

#else

   #error Unsupported architecture.

#endif
