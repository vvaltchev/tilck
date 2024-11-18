/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/modules.h>

#include <3rd_party/fdt_helper.h>
#include <libfdt.h>

#define BE16(x)   __builtin_bswap16(x)
#define BE32(x)   __builtin_bswap32(x)
#define BE64(x)   __builtin_bswap64(x)

#define QEMU_FWCFG_SELECTOR (qemu_fwcfg_base + 0x8UL)
#define QEMU_FWCFG_DATA     qemu_fwcfg_base
#define QEMU_FWCFG_ADDR     (qemu_fwcfg_base + 0x10UL)

#define fourcc_code(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | \
            ((u32)(c) << 16) | ((u32)(d) << 24))

#define QEMU_RAMFB_RGB888   fourcc_code('R', 'G', '2', '4')
#define QEMU_RAMFB_XRGB8888 fourcc_code('X', 'R', '2', '4')
#define QEMU_RAMFB_RGB565   fourcc_code('R', 'G', '1', '6')

struct qemu_ramfb_cfg {
   u64 addr;
   u32 format;
   u32 flags;
   u32 width;
   u32 height;
   u32 stride;
} __attribute__((packed));

struct qemu_fwcfg_dma_access {
   u32 cntl;
   u32 len;
   u64 addr;
} __attribute__((packed));

struct qemu_fwcfg_file {
   u32 size;
   u16 select;
   u16 reserved;
   char name[56];
} __attribute__((packed));

static ulong qemu_fwcfg_base;
static ulong simplefb_paddr;
static u32 simplefb_pitch;
static u32 simplefb_width;
static u32 simplefb_height;
static u32 simplefb_format;

static void
qemu_fwcfg_dma_trans(void *addr, u32 len, u32 cntl)
{
   struct qemu_fwcfg_dma_access access;

   access.cntl = BE32(cntl);
   access.len = BE32(len);
   access.addr = BE64((u64)addr);
   mmio_writeq((BE64((u64)&access)), (void *)QEMU_FWCFG_ADDR);

   while (BE32(access.cntl) & (u32)(~0x1)) { }
}

static void qemu_fwcfg_dma_sel_read(void *buf, u32 sel, u32 len)
{
   qemu_fwcfg_dma_trans(buf, len, (sel << 16) | 0x08 | 0x02);
}

static void qemu_fwcfg_dma_sel_write(void *buf, u32 sel, u32 len)
{
   qemu_fwcfg_dma_trans(buf, len, (sel << 16) | 0x08 | 0x10);
}

static void qemu_fwcfg_dma_read(void *buf, int len)
{
   qemu_fwcfg_dma_trans(buf, len, 0x02);
}

static int fdt_parse_fwcfg(void *fdt)
{
   int node, rc;
   u64 addr, size;
   char compat[] = "qemu,fw-cfg-mmio";

   node = fdt_node_offset_by_compatible(fdt, -1, compat);
   if (node < 0)
      return -EINVAL;

   rc = fdt_get_node_addr_size(fdt, node, 0, &addr, &size);
   if (rc)
      return -EINVAL;

   qemu_fwcfg_base = addr;
   return 0;
}

static int fdt_parse_simplefb(void *fdt)
{
   const fdt32_t *prop;
   int node, len, rc;
   u64 addr, size;
   char compat[] = "simple-framebuffer";

   node = fdt_node_offset_by_compatible(fdt, -1, compat);
   if (node < 0)
      return -EINVAL;

   prop = fdt_getprop(fdt, node, "width", &len);
   if (!prop)
      return -EINVAL;

   simplefb_width = fdt32_to_cpu(*prop);

   prop = fdt_getprop(fdt, node, "height", &len);
   if (!prop)
      return -EINVAL;

  simplefb_height = fdt32_to_cpu(*prop);

   prop = fdt_getprop(fdt, node, "stride", &len);
   if (!prop)
      return -EINVAL;

   simplefb_pitch = fdt32_to_cpu(*prop);

   rc = fdt_get_node_addr_size(fdt, node, 0, &addr, &size);
   if (rc)
      return -EINVAL;

   simplefb_paddr = addr;

   prop = fdt_getprop(fdt, node, "format", &len);
   if (!prop)
      return -EINVAL;

   if (!strcmp((void *)prop, "r8g8b8"))
      simplefb_format = QEMU_RAMFB_RGB888;
   else if (!strcmp((void *)prop, "x8r8g8b8"))
      simplefb_format = QEMU_RAMFB_XRGB8888;
   else if (!strcmp((void *)prop, "r5g6b5"))
      simplefb_format = QEMU_RAMFB_RGB565;

   return 0;
}

int init_ramfb(void *fdt)
{
   int rc;
   bool found = false;
   u32 count = 0, select = 0;
   struct qemu_fwcfg_file file;

   rc = fdt_parse_fwcfg(fdt);
   if (rc)
      return -EINVAL;

   rc = fdt_parse_simplefb(fdt);
   if (rc)
      return -EINVAL;

   qemu_fwcfg_dma_sel_read(&count, 0x19, sizeof(count));
   count = BE32(count);

   for (u32 i = 0; i < count; i++) {
      qemu_fwcfg_dma_read(&file, sizeof(file));
      if (!strcmp(file.name, "etc/ramfb")) {
         found = true;
         select = BE16(file.select);
         break;
      }
   }

   if (!found)
      return -ENODEV;

   struct qemu_ramfb_cfg ramfb_cfg = {
      .addr = BE64(simplefb_paddr),
      .format = BE32(simplefb_format),
      .flags = BE32(0),
      .width = BE32(simplefb_width),
      .height = BE32(simplefb_height),
      .stride = BE32(simplefb_pitch)
   };

   qemu_fwcfg_dma_sel_write(&ramfb_cfg, select, sizeof(ramfb_cfg));
   return 0;
}

