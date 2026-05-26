/* SPDX-License-Identifier: BSD-2-Clause */

/* violation: opening brace on same line as signature */
static int bad_one(void) {
   return 1;
}

/* compliant: brace on own line */
static int
good_one(void)
{
   return 2;
}
