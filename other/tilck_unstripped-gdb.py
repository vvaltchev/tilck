# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
import sys
import re

# Constants coming from CMake (this file gets pre-processed by CMake)
OTHER_DIR = "@CMAKE_SOURCE_DIR@/other"
# ---

sys.path.append(OTHER_DIR)
import gdb_scripts.base_utils as bu
import gdb_scripts.list_cmds
import gdb_scripts.get_cmds
import gdb_scripts.process_printer

# Init all the custom GDB commands
for cmd in bu.gdb_custom_cmds:
   cmd()

# Register all the regex pretty printers
gdb.printing.register_pretty_printer(
   gdb.current_objfile(),
   bu.regex_pretty_printers
)
