# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
import sys
import re

# Add Tilck's "other/" directory to our import path and import base_utils
sys.path.append("@CMAKE_SOURCE_DIR@/other")
import gdb_scripts.base_utils as bu

# Set the build config as early as possible using the constants coming from
# CMake (this file gets pre-processed by CMake)
bu.set_build_config(
   bu.BuildConfig(
      "@CMAKE_SOURCE_DIR@",
      int("@MAX_HANDLES@")
   )
)

# Import the rest of our gdb_scripts
import gdb_scripts.list_cmds
import gdb_scripts.get_cmds
import gdb_scripts.process_printer
import gdb_scripts.task_printer
import gdb_scripts.regs_printer

# Init all the custom GDB commands
for cmd in bu.gdb_custom_cmds:
   cmd()

# Register all the regex pretty printers
gdb.printing.register_pretty_printer(
   gdb.current_objfile(),
   bu.regex_pretty_printers
)
