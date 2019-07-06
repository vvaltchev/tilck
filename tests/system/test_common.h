/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include "devshell.h"

void create_test_file(const char *path, int n);
int remove_test_file(const char *path, int n);
void remove_test_file_expecting_success(const char *path, int n);
