/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/list.h>

struct module {

   const char *name;
   int priority;
   void (*init)(void);
};

void init_modules(void);
void register_module(struct module *m);

#define REGISTER_MODULE(m)                             \
   __attribute__((constructor))                        \
   static void __register_module(void)                 \
   {                                                   \
      register_module(m);                              \
   }


/*           Order of initialization of Tilck's modules            */

#define MOD_sysfs_prio                        10  /* first */
#define MOD_pci_prio                          20
#define MOD_acpi_prio                         30
#define MOD_kb_prio                           50
#define MOD_tracing_prio                     100
#define MOD_tty_prio                         200
#define MOD_fbdev_prio                       300
#define MOD_serial_prio                      400
#define MOD_sb16_prio                        410
#define MOD_dp_prio                         1000  /* last */
