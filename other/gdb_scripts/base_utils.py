# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
from collections import namedtuple

BuildConfig = namedtuple(
   "BuildConfig", [
      "CMAKE_SOURCE_DIR",
      "MAX_HANDLES",
      "KERNEL_BASE_VA"
   ]
)

type_task = gdb.lookup_type("struct task")
type_task_p = type_task.pointer()

type_process = gdb.lookup_type("struct process")
type_process_p = type_process.pointer()

list_node = gdb.lookup_type("struct list_node")
list_node_p = list_node.pointer()

gdb_custom_cmds = []
regex_pretty_printers = gdb.printing.RegexpCollectionPrettyPrinter("Tilck")

config = None

def set_build_config(obj):
   global config
   assert(config is None)
   assert(isinstance(obj, BuildConfig))
   config = obj

def register_new_custom_gdb_cmd(cmd_class):
   gdb_custom_cmds.append(cmd_class)

def register_tilck_regex_pp(name, regex, class_name):
   regex_pretty_printers.add_printer(name, regex, class_name)

def offset_of(type_name, field):
   return int(
      gdb.parse_and_eval(
         "((unsigned long)(&(({} *) 0)->{}))".format(type_name, field)
      )
   )

def container_of(elem_ptr, type_name, mem_name):
   off = offset_of(type_name, mem_name)
   expr = "(({} *)(((char *)0x{:08x}) - {}))".format(type_name, elem_ptr, off)
   return gdb.parse_and_eval(expr)

def select_field_in_list(arr, f, asString = False):

   if asString:
      return [ str(x[f]) for x in arr ]

   return [ x[f] for x in arr ]

def list_to_str_elems(arr):
   return [ str(x) for x in arr ]

def joined_str_list_with_field_select(arr, field = None, sep = ", "):

   if not field:
      return sep.join(list_to_str_elems(arr))

   return sep.join(select_field_in_list(arr, field, True))

def fixhex16(n):
   return "0x{:04x}".format(int(n))

def fixhex32(n):
   return "0x{:08x}".format(int(n))
