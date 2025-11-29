/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#ifndef __x86_64__
   #ifndef CLANGD
      #error This header can be used only when building for x86_64.
   #endif
#endif

static ALWAYS_INLINE ulong get_stack_ptr(void)
{
   ulong sp;

   asmVolatile("mov %%rsp, %0"
               : "=r" (sp)
               : /* no input */
               : /* no clobber */);

   return sp;
}
