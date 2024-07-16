/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

/* The size pointer returns the actual allocated size */
void *ioremap(ulong paddr, size_t size);
void iounmap(void *vaddr);
