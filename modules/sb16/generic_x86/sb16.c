/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_sb16.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/modules.h>

static void
init_sb16(void)
{
   /* do nothing, at the moment */
}

static struct module sb16_module = {

   .name = "sb16",
   .priority = MOD_sb16_prio,
   .init = &init_sb16,
};

REGISTER_MODULE(&sb16_module);
