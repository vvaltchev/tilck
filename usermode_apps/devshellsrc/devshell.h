/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#define KB (1024)
#define MB (1024 * 1024)

#define MAX_ARGS 16

#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[93m"
#define RESET_ATTRS   "\033[0m"

#define RDTSC() __builtin_ia32_rdtsc()
#define FORK_TEST_ITERS (250 * MB)

void run_if_known_command(const char *cmd, int argc, char **argv);
void dump_list_of_commands(void);
int read_command(char *buf, int buf_size);
