/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>

#define mb()    asmVolatile("fence iorw, iorw" : : : "memory")
#define rmb()   asmVolatile("fence ir, ir" : : : "memory")
#define wmb()   asmVolatile("fence ow, ow" : : : "memory")

static ALWAYS_INLINE u8
mmio_readb(const volatile void *addr)
{
   u8 val = *(volatile u8 *)addr;
   rmb();

   return val;
}

static ALWAYS_INLINE u16
mmio_readw(const volatile void *addr)
{
   u16 val = *(volatile u16 *)addr;
   rmb();

   return val;
}

static ALWAYS_INLINE u32
mmio_readl(const volatile void *addr)
{
   u32 val = *(volatile u32 *)addr;
   rmb();

   return val;
}

static ALWAYS_INLINE u64
mmio_readq(const volatile void *addr)
{
   u64 val = *(volatile u64 *)addr;
   rmb();

   return val;
}

static ALWAYS_INLINE void
mmio_writeb(u8 val, volatile void *addr)
{
   wmb();
   *(volatile u8 *)addr = val;
}

static ALWAYS_INLINE void
mmio_writew(u16 val, volatile void *addr)
{
   wmb();
   *(volatile u16 *)addr = val;
}

static ALWAYS_INLINE void
mmio_writel(u32 val, volatile void *addr)
{
   wmb();
   *(volatile u32 *)addr = val;
}

static ALWAYS_INLINE void
mmio_writeq(u64 val, volatile void *addr)
{
   wmb();
   *(volatile u64 *)addr = val;
}
