/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>


static void
sysfs_fail_to_register_obj(const char *name)
{
   panic("sysfs: unable to register object '%s'", name);
}

#include <tilck_gen_headers/generated_config.h>
