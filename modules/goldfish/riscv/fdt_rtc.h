/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/datetime.h>

struct fdt_rtc {
   struct list_node node;
   const struct fdt_match *id_table;
   int (*init)(void *fdt, int node, const struct fdt_match *match);
};

struct fdt_rtc_dev {
   void *priv;
   int (* rtc_get)(void *priv, struct datetime *d);
};

#define REGISTER_FDT_RTC(__name, __id_table, __init)      \
                                                          \
   static struct fdt_rtc fdt_rtc_##__name = {             \
      .id_table = __id_table,                             \
      .init = __init                                      \
   };                                                     \
                                                          \
   __attribute__((constructor))                           \
   static void __register_fdt_rtc_##__name(void) {        \
      fdt_rtc_drv_register(&fdt_rtc_##__name);            \
   }

void fdt_rtc_drv_register(struct fdt_rtc *drv);
int fdt_rtc_register(void *priv, int (* rtc_get)(void *, struct datetime *));

