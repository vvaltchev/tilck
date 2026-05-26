/* SPDX-License-Identifier: BSD-2-Clause */

extern int* g_p;        /* Type* var -- violation 1 */

void foo(char * arg)    /* Type * var -- violation 2 */
{
   int b;
   b = (arg != 0) ? 1 : 0;
   g_p = &b;
}
