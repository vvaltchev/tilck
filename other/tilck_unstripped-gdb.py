# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
import sys
import re

# Constants coming from CMake (this file gets pre-processed by CMake)
OTHER_DIR = "@CMAKE_SOURCE_DIR@/other"
# ---

sys.path.append(OTHER_DIR)
from gdb_scripts.base_utils import *
from gdb_scripts.tasks import *
from gdb_scripts.list_cmds import *
from gdb_scripts.get_cmds import *
from gdb_scripts.process_printer import *

def build_pretty_printers():
   pp = gdb.printing.RegexpCollectionPrettyPrinter("Tilck")
   pp.add_printer('process', '^process$', printer_struct_process)
   return pp

def build_gdb_cmds():
   cmd_list_tasks()
   cmd_list_procs()
   cmd_get_proc()
   cmd_get_task()

# ------------------ MAIN ------------------

build_gdb_cmds()

gdb.printing.register_pretty_printer(
   gdb.current_objfile(),
   build_pretty_printers()
)
