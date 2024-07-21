/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/assert.h>

#include <tilck/common/printk.h>
#include <tilck/common/string_util.h>
#include <tilck/common/arch/riscv/riscv_utils.h>
#include <tilck/kernel/hal.h>
#include <3rd_party/fdt_helper.h>
#include <libfdt.h>

#define ANDESTECH_VENDOR_ID   0x31e
#define SIFIVE_VENDOR_ID      0x489
#define THEAD_VENDOR_ID       0x5b7

/*
 * Svpbmt custom page attribute bits:
 */
#define _PAGE_PMA_SVPBMT       (0UL)
#define _PAGE_NOCACHE_SVPBMT   (1UL << 61)
#define _PAGE_IO_SVPBMT        (1UL << 62)
#define _PAGE_MTMASK_SVPBMT    (_PAGE_NOCACHE_SVPBMT | _PAGE_IO_SVPBMT)

/*
 * T-Head custom page attribute bits:
 */
#define _PAGE_PMA_THEAD      ((1UL << 62) | (1UL << 61) | (1UL << 60))
#define _PAGE_NOCACHE_THEAD  (0UL)
#define _PAGE_IO_THEAD       (1UL << 63)
#define _PAGE_MTMASK_THEAD   (_PAGE_PMA_THEAD | _PAGE_IO_THEAD | (1UL << 59))

volatile struct riscv_cpu_features riscv_cpu_features;
static char isa_string_buf[128];
static char isa_ext_string_buf[1024];
static char model_name[64];

static const char *isa_exts_names[] =
{
   "i",
   "m",
   "a",
   "f",
   "d",
   "q",
   "c",
   "b",
   "k",
   "j",
   "p",
   "v",
   "h",
   "zicbom",
   "zicboz",
   "zicntr",
   "zicsr",
   "zifencei",
   "zihintpause",
   "zihpm",
   "zba",
   "zbb",
   "zbs",
   "smaia",
   "ssaia",
   "sscofpmf",
   "sstc",
   "svinval",
   "svnapot",
   "svpbmt",
};

static int fdt_parse_model(void *fdt)
{
   int node, len;
   const fdt32_t *prop;

   node = fdt_path_offset(fdt, "/");
   if (node < 0)
      return -1;

   prop = fdt_getprop(fdt, node, "model", &len);
   if (!prop || !len)
      return -1;

   strncpy(model_name, (const char *)prop, sizeof(model_name));

   return 0;
}

static int
fdt_parse_hart_id(void *fdt, int cpu_offset, u32 *hartid)
{
   int len;
   const void *prop;
   const fdt32_t *val;

   if (!fdt || cpu_offset < 0)
      return -1;

   prop = fdt_getprop(fdt, cpu_offset, "device_type", &len);
   if (!prop || !len)
      return -1;
   if (strncmp (prop, "cpu", strlen ("cpu")))
      return -1;

   val = fdt_getprop(fdt, cpu_offset, "reg", &len);
   if (!val || len < (int)sizeof(fdt32_t))
      return -1;

   if (len > (int)sizeof(fdt32_t))
      val++;

   if (hartid)
      *hartid = fdt32_to_cpu(*val);

   return 0;
}

static int fdt_parse_isa_extensions(void *fdt)
{
   int cpu_node, cpus_node, len, rc;
   u32 hart_id;
   const fdt32_t *prop;

   cpus_node = fdt_path_offset(fdt, "/cpus");
   if (cpus_node < 0)
      return -1;

   fdt_for_each_subnode(cpu_node, fdt, cpus_node) {

      rc = fdt_parse_hart_id(fdt, cpu_node, &hart_id);
      if (rc)
         return rc;

      prop = fdt_getprop(fdt, cpu_node, "device_type", NULL);

      if (prop &&
         !strcmp((void *)prop, "cpu") &&
          hart_id == get_boothartid())
      {
         prop = fdt_getprop(fdt, cpu_node, "riscv,isa", &len);
         if (prop && len) {
            strncpy(isa_string_buf, (void *)prop,
                     sizeof(isa_string_buf));
         }

         prop = fdt_getprop(fdt, cpu_node, "riscv,isa-extensions", &len);
         if (prop && len) {
            memcpy(isa_ext_string_buf, (void *)prop,
                   MIN((ulong)len, sizeof(isa_ext_string_buf)));
         }

         return 0;
      }
   }

   return -1;
}

static bool isa_ext_string_match(const char *string)
{
   char *buf = isa_ext_string_buf;
   char *end = buf + strnlen(buf, sizeof(isa_ext_string_buf));
   char *chr;

   // try isa_ext_string_buf

   for (char *ptr = buf; ptr < end; ptr += strlen(ptr) + 1) {

      if (strcmp(string, ptr) == 0)
         return true;
   }

   /*
    * try isa_string_buf, Some platforms(e.g. qemu-virt) may not have
    * riscv,isa-extensions property defined in the device tree
    */
   buf = isa_string_buf;
   if (strncmp(buf, "rv64", 4) && strncmp(buf, "rv32", 4))
      panic("riscv: \"riscv,isa\" property not defined in device tree");

   if (strlen(string) == 1) {

      buf = isa_string_buf;
      chr = strchr(buf, '_');
      end = chr ? chr : (buf + strnlen(buf, sizeof(isa_string_buf)));

      for (char *ptr = buf + 4; ptr < end; ptr++) {

         if (*ptr == *string)
            return true;
      }

   } else {

      buf = isa_string_buf;
      end = buf + strnlen(buf, sizeof(isa_string_buf));
      size_t len;

      for (char *ptr = buf; ptr < end; ptr += len + 1) {

         chr = strchr(ptr, '_');
         len = (chr ? chr : end) - ptr;
         if (strncmp(string, ptr, len) == 0)
            return true;
   }
   }

   return false;
}

/*
 * We must set the contents of riscv_cpu_features before early_init_paging(),
 * because when setup the kernel mapping, we have to make sure the vendor
 * defined page attribute bits are set correctly in riscv_cpu_features.
 */
void early_get_cpu_features(void)
{
   struct riscv_cpu_features *f = (void *)&riscv_cpu_features;

   bool *flags = (bool *)&f->isa_exts;

   if (sbi_get_spec_version().error) {

      // legacy extensions
      f->vendor_id = 0;
      f->arch_id = 0;
      f->imp_id = 0;
   } else {

      f->vendor_id = sbi_get_mvendorid().value;
      f->arch_id = sbi_get_marchid().value;
      f->imp_id = sbi_get_mimpid().value;
   }

   fdt_parse_model(fdt_get_address());
   fdt_parse_isa_extensions(fdt_get_address());

   bzero(&f->isa_exts, sizeof(f->isa_exts));

   for (u32 i = 0; i < ARRAY_SIZE(isa_exts_names); i++) {

      flags[i] = isa_ext_string_match(isa_exts_names[i]) ? true : false;
   }

   if (f->isa_exts.svpbmt) {

      f->page_mtmask = _PAGE_MTMASK_SVPBMT;
      f->page_cb =     _PAGE_PMA_SVPBMT;
      f->page_wt =     _PAGE_NOCACHE_SVPBMT;
      f->page_io =     _PAGE_IO_SVPBMT;

   } else if (f->vendor_id == THEAD_VENDOR_ID) {

      f->page_mtmask = _PAGE_MTMASK_THEAD;
      f->page_cb =     _PAGE_PMA_THEAD;
      f->page_wt =     _PAGE_NOCACHE_THEAD;
      f->page_io =     _PAGE_IO_THEAD;
   }
}

void get_cpu_features(void)
{
   /*
    * Already done in early_get_cpu_features(),
    * here just dump cpu features
    */
   dump_riscv_features();
}

void dump_riscv_features(void)
{
   char buf[256];
   u32 w = 0;

   printk("MODEL: %s\n", model_name);
   printk("VENDOR-ID: 0x%lx ", riscv_cpu_features.vendor_id);
   printk("ARCH-ID: 0x%lx ", riscv_cpu_features.arch_id);
   printk("IMP-ID: 0x%lx\n", riscv_cpu_features.imp_id);
   printk("CPU: %s\n", isa_string_buf);
   printk("isa-extensions: ");

   bool *flags = (bool *)&riscv_cpu_features.isa_exts;

   for (u32 i = 0; i < ARRAY_SIZE(isa_exts_names); i++) {

      if (flags[i])
         w += (u32)snprintk(buf + w, sizeof(buf) - w, "%s ", isa_exts_names[i]);

      if (w >= 60) {
         printk("%s\n", buf);
         w = 0;
         buf[0] = 0;
      }
   }

   if (w)
      printk("%s\n", buf);
}
