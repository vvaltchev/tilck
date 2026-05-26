/* SPDX-License-Identifier: BSD-2-Clause */

int foo()
{
   return 42;
}

int bar()
{
   return 1;
}

extern "C" {

int baz(void)
{
   return 0;
}

}
