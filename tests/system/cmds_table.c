/* SPDX-License-Identifier: BSD-2-Clause */
#include "devshell.h"

#define CMD_ENTRY(name, len, enabled) int cmd_##name(int argc, char **argv);
#include "cmds_table.h"

#undef CMD_ENTRY
#define CMD_ENTRY(name, len, enabled) {#name, cmd_ ## name, len, enabled},

static struct test_cmd_entry _cmds_table[] =
{
   #include "cmds_table.h"
   {NULL, NULL, 0, 0}
};

struct test_cmd_entry *cmds_table = _cmds_table;

