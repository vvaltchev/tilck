/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

static ALWAYS_INLINE u32
mmio_read32(const volatile void *io_addr)
{
    COMPILER_BARRIER();
    u32 val = *(volatile u32*) io_addr;
    COMPILER_BARRIER();

    return val;
}

static ALWAYS_INLINE void
mmio_write32(u32 val, volatile void *io_addr)
{
    COMPILER_BARRIER();
    *(volatile u32*) io_addr = val;
    COMPILER_BARRIER();
}
