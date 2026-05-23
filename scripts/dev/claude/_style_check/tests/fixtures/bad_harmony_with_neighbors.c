/* SPDX-License-Identifier: BSD-2-Clause */

void short_calls(void)
{
   int a = 0;
   int b = 1;
   a = b + 1;
   b = a - 1;
   int x = some_long_function_call(arg1, arg2, arg3, arg4) + extra;
   a = b + 2;
   b = a - 2;
   a = b * 3;
   b = a / 3;
}
