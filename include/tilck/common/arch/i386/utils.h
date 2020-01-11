/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#ifndef __i386__
   #error This header can be used only when building for ia32.
#endif

static ALWAYS_INLINE ulong get_stack_ptr(void)
{
   ulong sp;

   asmVolatile("mov %%esp, %0"
               : "=r" (sp)
               : /* no input */
               : /* no clobber */);

   return sp;
}
