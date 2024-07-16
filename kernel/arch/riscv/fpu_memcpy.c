/* SPDX-License-Identifier: BSD-2-Clause */

#define __FPU_MEMCPY_C__

/*
 * The code in this translation unit, in particular the *single* funcs defined
 * in fpu_memcpy.h have to be FAST, even in debug builds. It is important the
 * hot-patched fpu_cpy_single_256_nt, created by copying the once of the
 * fpu_cpy_*single* funcs to just execute the necessary MOVs and then just a
 * RET. No prologue/epilogue, no frame pointer, no stack variables.
 * NOTE: clearly, the code works well even if -O0 and without the PRAGMAs
 * below. Just, we want it to be fast in debug builds to improve the user
 * experience there.
 */

#if defined(__GNUC__) && !defined(__clang__)
   #pragma GCC optimize "-O3"
   #pragma GCC optimize "-fomit-frame-pointer"
#endif

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/arch/riscv/fpu_memcpy.h>

void
memcpy256_failsafe(void *dest, const void *src, u32 n)
{
   memcpy32(dest, src, n * 8);
}

FASTCALL void
memcpy_single_256_failsafe(void *dest, const void *src)
{
   memcpy32(dest, src, 8);
}
