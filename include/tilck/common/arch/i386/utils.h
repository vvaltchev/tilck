/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#ifndef __i386__
   #error This header can be used only when building for ia32.
#endif

static ALWAYS_INLINE uptr get_stack_ptr(void)
{
   uptr sp;

   asmVolatile("mov %%esp, %0"
               : "=r" (sp)
               : /* no input */
               : /* no clobber */);

   return sp;
}
