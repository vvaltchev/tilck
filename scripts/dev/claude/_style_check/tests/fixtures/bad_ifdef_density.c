/* SPDX-License-Identifier: BSD-2-Clause */
#include <tilck/common/basic_defs.h>

#ifdef FOO
int x = 1;
#else
int x = 2;
#endif

#ifndef BAR
int y = 3;
#endif
