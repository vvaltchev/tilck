/* SPDX-License-Identifier: BSD-2-Clause */

extern int do_thing(void);

/* XXX: this should be TODO instead */
int helper(void)
{
   /* XXX: handle the broken case */
   return do_thing();
}

void other(void)
{
   /* TODO: fine, this stays */
   /* FIXME: also fine */
   /* XXX: another bad tag */
   do_thing();
}
