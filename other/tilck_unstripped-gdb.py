# SPDX-License-Identifier: BSD-2-Clause

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
      int("@MAX_HANDLES@"),
      int("@BASE_VA@", 16),
   )
)

# Import the rest of our gdb_scripts
import gdb_scripts.list_cmds
import gdb_scripts.get_cmds
import gdb_scripts.process_printer
import gdb_scripts.task_printer
import gdb_scripts.regs_printer
import gdb_scripts.mobj_printer
import gdb_scripts.fs_handle_printer
import gdb_scripts.mi_printer

# Init all the custom GDB commands
bu.init_all_custom_cmds()

# Register all the regex pretty printers
gdb.printing.register_pretty_printer(
   gdb.current_objfile(),
   bu.regex_pretty_printers
)
