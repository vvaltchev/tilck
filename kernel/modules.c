/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/sort.h>

static u32 mods_count;
static struct module *modules[32];

void register_module(struct module *m)
{
   ASSERT(mods_count < ARRAY_SIZE(modules) - 1);
   modules[mods_count++] = m;
}

static sptr mod_cmp_func(const void *a, const void *b)
{
   const struct module * const *ma = a;
   const struct module * const *mb = b;
   return (*ma)->priority - (*mb)->priority;
}

void init_modules(void)
{
   insertion_sort_ptr(modules, mods_count, &mod_cmp_func);

   for (u32 i = 0; i < mods_count; i++) {
      struct module *m = modules[i];
      m->init();
   }
}
