/* SPDX-License-Identifier: BSD-2-Clause */

void foo(void)
{
	int x = 1;       /* tab-indented: violation */
	int y = 2;
   int z = 3;       /* 3-space: ok */
}
