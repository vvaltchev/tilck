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

   /*
    * This might be the stupidest way O(P * N) to initialize the modules in
    * order but, for a very few modules, it's totally good enough.
    *
    * Of course, in order to have more granularity for priorities and more
    * modules P * N starts to become too bad (e.g. P = 1000, N = 20), even if,
    * given that we're in the initialization, we will still be fast enough, it
    * will be worth implementing something better.
    *
    * TODO: improve init_modules().
    */
   for (int p = 0; p < LOWEST_MOD_PRIORITY; p++) {

      list_for_each_ro(mod, &modules_list, node) {

         if (mod->priority == p)
            mod->init();
      }
   }
}
