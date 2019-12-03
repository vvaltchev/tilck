/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/modules.h>

static struct list modules_list = make_list(modules_list);

void register_module(struct module *m)
{
   list_add_tail(&modules_list, &m->node);
}

void init_modules(void)
{
   struct module *mod;

   list_for_each_ro(mod, &modules_list, node) {
      mod->init();
   }
}
