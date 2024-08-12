/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <3rd_party/fdt_helper.h>
#include <libfdt.h>
#include "fdt_rtc.h"

static struct list drv_list = STATIC_LIST_INIT(drv_list);
static struct fdt_rtc_dev *rtc = NULL;

int fdt_rtc_register(void *priv, int (* rtc_get)(void *, struct datetime *))
{
   rtc = kzmalloc(sizeof(*rtc));
   if (!rtc)
      return -ENOMEM;

   rtc->priv = priv;
   rtc->rtc_get = rtc_get;
   return 0;
}

void fdt_rtc_drv_register(struct fdt_rtc *drv)
{
   list_add_tail(&drv_list, &drv->node);
}

static void init_fdt_rtc(void)
{
   int node;
   bool enabled;
   const struct fdt_match *matched;
   struct fdt_rtc *drv;

   void *fdt = fdt_get_address();

   for (node = fdt_next_node(fdt, -1, NULL);
        node >= 0;
        node = fdt_next_node(fdt, node, NULL)) {

      list_for_each_ro(drv, &drv_list, node) {
         enabled = fdt_node_is_enabled(fdt, node);
         matched = fdt_match_node(fdt, node, drv->id_table);

         if (enabled && matched) {
            drv->init(fdt, node, matched);
            return; // Only one rtc device is initialized
         }
      }
   }
}

void hw_read_clock_cmos(struct datetime *out)
{
   static int first_run = 1;

   if (first_run) {
      init_fdt_rtc();
      first_run = 0;
   }

   if (rtc)
      rtc->rtc_get(rtc->priv, out);
   else
      timestamp_to_datetime(0, out);
}

