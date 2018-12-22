/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

struct gcov_info;

typedef u64 gcov_type;

void __gcov_merge_add(gcov_type *counters, u32 n) { }
void __gcov_exit(void) { }
void __gcov_init(struct gcov_info *info) { }
