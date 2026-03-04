/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

static ALWAYS_INLINE u32 mmio_read32(volatile u32 *io_addr, u32 off)
{
    ASSERT(off % 4 == 0);
    return *(io_addr + off/4);
}

static ALWAYS_INLINE void mmio_write32(volatile u32 *io_addr, u32 off, u32 val)
{
    ASSERT(off % 4 == 0);
    *(io_addr + off/4) = val;
}
