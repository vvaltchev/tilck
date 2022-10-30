/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/string_util.h>

s32 tilck_strtol32(const char *s, const char **endptr, int base, int *err);
s64 tilck_strtol64(const char *s, const char **endptr, int base, int *err);
u32 tilck_strtoul32(const char *s, const char **endptr, int base, int *err);
u64 tilck_strtoul64(const char *s, const char **endptr, int base, int *err);
