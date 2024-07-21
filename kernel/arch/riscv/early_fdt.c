/* SPDX-License-Identifier: BSD-2-Clause */
#include <tilck/common/basic_defs.h>
#include <tilck/common/boot.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/hal.h>
#include <3rd_party/fdt_helper.h>
#include <libfdt.h>
#include <multiboot.h>
#include "paging_int.h"

#define CMDLINE_BUF_SZ 1024

struct simplefb_bitfield {
   u8 offset;   /* beginning of bitfield */
   u8 size;       /* length of bitfield */
};

struct simplefb_format {
   const char *name;
   u8 bpp;  /* bits per pixel */
   struct simplefb_bitfield red;
   struct simplefb_bitfield green;
   struct simplefb_bitfield blue;
   struct simplefb_bitfield alpha;
};

void *fdt_blob;   //save virtual address of fdt

static ulong initrd_paddr;
static ulong initrd_size;

static multiboot_info_t *mbi;
static multiboot_module_t *mod;
static multiboot_memory_map_t *mmmap;

static int mmmap_count = 0;
static char *cmdline_buf;

static struct simplefb_format simplefb_formats[] = {
   {"r5g6b5", 16, {11, 5}, {5, 6}, {0, 5}, {0, 0}},
   {"r5g5b5a1", 16, {11, 5}, {6, 5}, {1, 5}, {0, 1}},
   {"x1r5g5b5", 16, {10, 5}, {5, 5}, {0, 5}, {0, 0}},
   {"a1r5g5b5", 16, {10, 5}, {5, 5}, {0, 5}, {15, 1}},
   {"r8g8b8", 24, {16, 8}, {8, 8}, {0, 8}, {0, 0}},
   {"x8r8g8b8", 32, {16, 8}, {8, 8}, {0, 8}, {0, 0}},
   {"a8r8g8b8", 32, {16, 8}, {8, 8}, {0, 8}, {24, 8}},
   {"x8b8g8r8", 32, {0, 8}, {8, 8}, {16, 8}, {0, 0}},
   {"a8b8g8r8", 32, {0, 8}, {8, 8}, {16, 8}, {24, 8}},
   {"x2r10g10b10", 32, {20, 10}, {10, 10}, {0, 10}, {0, 0}},
   {"a2r10g10b10", 32, {20, 10}, {10, 10}, {0, 10}, {30, 2}},
};

void alloc_mbi(void)
{
   void *mbi_memtop;

   /*
    * Place multiboot information at the top of
    * 8 MB identity mapping space.
    */
   mbi_memtop = (void *)(LIN_VA_TO_PA(BASE_VA) + EARLY_MAP_SIZE);

   mbi = (multiboot_info_t *)((ulong)mbi_memtop - sizeof(*mbi));
   bzero(mbi, sizeof(*mbi));

   mod = (multiboot_module_t *)((ulong)mbi - sizeof(*mod));
   bzero(mod, sizeof(*mod));

   cmdline_buf = (char *)mod - CMDLINE_BUF_SZ;
   bzero(cmdline_buf, CMDLINE_BUF_SZ);
}

static void
add_multiboot_mmap(u64 addr, u64 size, u32 type)
{
   /* Allocate a new multiboot_memory_map */
   if (!mmmap)
      mmmap = (void *)(cmdline_buf - sizeof(multiboot_memory_map_t));
   else
      mmmap--;

   mmmap_count++;

   bzero(mmmap, sizeof(multiboot_memory_map_t));

   if (addr < mbi->mem_lower * KB)
      mbi->mem_lower = (u32)(addr / KB);

   if (addr + size > mbi->mem_upper * KB)
      mbi->mem_upper = (u32)((addr + size) / KB);

   *mmmap = (multiboot_memory_map_t) {
      .size = sizeof(multiboot_memory_map_t) - sizeof(u32),
      .addr = addr,
      .len = size,
      .type = type,
   };
}

void setup_multiboot_info(ulong ramdisk_paddr, ulong ramdisk_size)
{
   mbi->flags |= MULTIBOOT_INFO_MEMORY;

   if (cmdline_buf[0]) {
      mbi->flags |= MULTIBOOT_INFO_CMDLINE;
      mbi->cmdline = (ulong)cmdline_buf;
   }

   mbi->flags |= MULTIBOOT_INFO_MODS;
   mbi->mods_addr = (ulong)mod;
   mbi->mods_count = 1;
   mod->mod_start = ramdisk_paddr;
   mod->mod_end = mod->mod_start + ramdisk_size;

   mbi->flags |= MULTIBOOT_INFO_MEM_MAP;
   mbi->mmap_addr = (ulong)mmmap;
   mbi->mmap_length = mmmap_count * sizeof(multiboot_memory_map_t);
}

static int
fdt_get_node_linux_usable_memory(void *fdt, int node, int index,
                                 uint64_t *addr, uint64_t *size)
{
   int len, i;
   int cell_addr, cell_size;
   const fdt32_t *prop_addr, *prop_size;
   uint64_t temp = 0;

   if (!fdt || node < 0 || index < 0)
      return -EINVAL;

   cell_addr = fdt_address_cells(fdt, 0);
   if (cell_addr < 1)
      return -ENODEV;

   cell_size = fdt_size_cells(fdt, 0);
   if (cell_size < 0)
      return -ENODEV;

   prop_addr = fdt_getprop(fdt, node, "linux,usable-memory", &len);
   if (!prop_addr)
      return -ENODEV;

   if ((len / sizeof(u32)) <= (ulong)(index * (cell_addr + cell_size)))
      return -EINVAL;

   prop_addr = prop_addr + (index * (cell_addr + cell_size));
   prop_size = prop_addr + cell_addr;

   if (addr) {
      for (i = 0; i < cell_addr; i++)
         temp = (temp << 32) | fdt32_to_cpu(*prop_addr++);
      *addr = temp;
   }

   temp = 0;

   if (size) {
      for (i = 0; i < cell_size; i++)
         temp = (temp << 32) | fdt32_to_cpu(*prop_size++);
      *size = temp;
   }

   return 0;
}

static int
fdt_add_multiboot_mmap(void *fdt, int node, u32 type)
{
   int nomem = 1, index = 0;
   u64 addr, size;

   while (fdt_get_node_linux_usable_memory(fdt, node, index,
                                           &addr, &size) == 0)
   {

      if (size == 0)
         continue;

      index++;
      nomem = 0;

      add_multiboot_mmap(addr, size, type);
   }

   if (!nomem)
      return 0;

   while (fdt_get_node_addr_size(fdt, node,
                                 index, &addr, &size) == 0)
   {

      if (size == 0)
         continue;

      index++;
      nomem = 0;

      add_multiboot_mmap(addr, size, type);
   }

   return nomem;
}

static inline u64
fdt_read64(const fdt32_t *cell, size_t size)
{
   u64 val_64 = (u64)fdt32_to_cpu(*(cell++));

   if (size >= 8)
      val_64 = (val_64 << 32) | fdt32_to_cpu(*cell);

   return val_64;
}

static int fdt_parse_chosen(void *fdt)
{
   u64 start, end;
   int node, len;
   const fdt32_t *prop;

   node = fdt_path_offset(fdt, "/chosen");
   if (node < 0)
      node = fdt_path_offset(fdt, "/chosen@0");
   if (node < 0)
      return -EINVAL;

   /* Try find initrd properties */
   prop = fdt_getprop(fdt, node, "linux,initrd-start", &len);
   if (!prop)
      goto parse_cmd;
   start = fdt_read64(prop, len);

   prop = fdt_getprop(fdt, node, "linux,initrd-end", &len);
   if (!prop)
      goto parse_cmd;
   end = fdt_read64(prop, len);

   if (start > end)
      goto parse_cmd;

   /* It is assumed that initrd is located at low 32-bit address here */
   initrd_paddr = (u32)start;
   initrd_size = (u32)end - initrd_paddr;

parse_cmd:
   /* Try find kernel command line */
   prop = fdt_getprop(fdt, node, "bootargs", &len);
   if (prop && len)
      strncpy(cmdline_buf, (const char *)prop, CMDLINE_BUF_SZ);

   return 0;
}

static int fdt_parse_memory(void *fdt)
{
   int node, nomem = 1;

   fdt_for_each_subnode(node, fdt, 0) {
      const char *type = fdt_getprop(fdt, node, "device_type", NULL);

      /* We are scanning "memory" nodes only */
      if (type == NULL || strcmp(type, "memory") != 0)
         continue;

      if (!fdt_node_is_enabled(fdt, node))
         continue;

      nomem = fdt_add_multiboot_mmap(fdt, node, MULTIBOOT_MEMORY_AVAILABLE);
   }

   return nomem;
}

static int fdt_parse_reserved_memory(void *fdt)
{
   int n;
   u64 base, size;
   int node, child;

   /* process fdt /reserved-memory node */
   node = fdt_path_offset(fdt, "/reserved-memory");
   if (node < 0)
      return 0;

   fdt_for_each_subnode(child, fdt, node) {
      const fdt32_t *reg;
      int len;

      if (!fdt_node_is_enabled(fdt, child))
         continue;

      reg = fdt_getprop(fdt, child, "reg", &len);
      if (reg == NULL)
         continue;

      fdt_add_multiboot_mmap(fdt, child, MULTIBOOT_MEMORY_RESERVED);
   }

   /* Additionally process fdt header /memreserve/ fields */
   for (n = 0; ; n++) {
      fdt_get_mem_rsv(fdt, n, &base, &size);
      if (!size)
         break;

      add_multiboot_mmap(base, size, MULTIBOOT_MEMORY_RESERVED);
   }

   return 0;
}

static int fdt_parse_framebuffer(void *fdt)
{
   const fdt32_t *prop;
   int node, len, rc;
   u64 addr, size;
   struct simplefb_format *format;
   char compat[] = "simple-framebuffer";

   node = fdt_node_offset_by_compatible(fdt, -1, compat);
   if (node < 0)
      return 0;

   prop = fdt_getprop(fdt, node, "width", &len);
   if (!prop)
      return 0;

   mbi->framebuffer_width = fdt32_to_cpu(*prop);

   prop = fdt_getprop(fdt, node, "height", &len);
   if (!prop)
      return 0;

   mbi->framebuffer_height = fdt32_to_cpu(*prop);

   prop = fdt_getprop(fdt, node, "stride", &len);
   if (!prop)
      return 0;

   mbi->framebuffer_pitch = fdt32_to_cpu(*prop);

   rc = fdt_get_node_addr_size(fdt, node, 0, &addr, &size);
   if (rc)
      return 0;

   mbi->framebuffer_addr = addr;

   prop = fdt_getprop(fdt, node, "format", &len);
   if (!prop)
      return 0;

   for (int i = 0; i < ARRAY_SIZE(simplefb_formats); i++) {

      if (strcmp((void *)prop, simplefb_formats[i].name))
         continue;

      format = &simplefb_formats[i];
      mbi->framebuffer_bpp = format->bpp;
      mbi->framebuffer_red_field_position = format->red.offset;
      mbi->framebuffer_red_mask_size = format->red.size;
      mbi->framebuffer_green_field_position = format->green.offset;
      mbi->framebuffer_green_mask_size = format->green.size;
      mbi->framebuffer_blue_field_position = format->blue.offset;
      mbi->framebuffer_blue_mask_size = format->blue.size;

      break;
   }

   mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
   mbi->flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;

   return 0;
}

/*
 * Parse flattened device tree(fdt), translate memory layout,
 * kernel command line and framebuffer into multiboot format used by tilck.
 */
multiboot_info_t *parse_fdt(void *fdt_pa)
{
   /* check device tree validity */
   if (fdt_check_header(fdt_pa))
      return NULL;

   alloc_mbi();

   fdt_parse_chosen(fdt_pa);
   fdt_parse_memory(fdt_pa);
   fdt_parse_reserved_memory(fdt_pa);
   fdt_parse_framebuffer(fdt_pa);

   setup_multiboot_info(initrd_paddr, initrd_size);

   fdt_blob = PA_TO_LIN_VA(fdt_pa);
   return mbi;
}
