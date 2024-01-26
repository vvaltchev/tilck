/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/datetime.h>
#include <3rd_party/fdt_helper.h>
#include <libfdt.h>
#include "fdt_rtc.h"

#define GOLDFISH_TIME_LOW   0x00
#define GOLDFISH_TIME_HIGH  0x04

struct goldfish_rtc {
   void *base;
   ulong paddr;
   ulong size;
};

static int goldfish_rtc_get(void *priv, struct datetime *out)
{
   struct goldfish_rtc *rtc = priv;
   u64 time_h, time_l, timens;
   s64 secs;

   time_l = mmio_readl(rtc->base + GOLDFISH_TIME_LOW);
   time_h = mmio_readl(rtc->base + GOLDFISH_TIME_HIGH);
   timens = (time_h << 32) | time_l;

   secs = timens / 1000000000;
   timestamp_to_datetime(secs, out);

   return 0;
}

int goldfish_rtc_init(void *fdt, int node, const struct fdt_match *match)
{
   struct goldfish_rtc *rtc;
   u64 addr, size;
   int rc;

   rc = fdt_get_node_addr_size(fdt, node, 0, &addr, &size);
   if (rc < 0 || !addr || !size)
      return -ENODEV;

   rtc = kzalloc_obj(struct goldfish_rtc);
   if (!rtc) {
      printk("goldfish_rtc: ERROR: oom!\n");
      return -ENOMEM;
   }

   rtc->paddr = addr;
   rtc->size = size;

   rtc->base = ioremap(rtc->paddr, rtc->size);
   if (!rtc->base) {
      printk("goldfish_rtc: ERROR: ioremap failed for %p\n",
            (void *)rtc->paddr);
      rc = -EIO;
      goto bad;
   }

   fdt_rtc_register(rtc, goldfish_rtc_get);

   return 0;

bad:
   kfree(rtc);
   return rc;
}

static const struct fdt_match goldfish_rtc_ids[] = {
   {.compatible = "google,goldfish-rtc"},
   { }
};

REGISTER_FDT_RTC(goldfish_rtc, goldfish_rtc_ids, goldfish_rtc_init)

