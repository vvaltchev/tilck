# SPDX-License-Identifier: BSD-2-Clause

import gdb # pylint: disable=import-error
from collections import namedtuple
from . import tilck_types as tt

BuildConfig = namedtuple(
   "BuildConfig", [
      "CMAKE_SOURCE_DIR",
      "MAX_HANDLES",
      "BASE_VA"
   ]
)

gdb_custom_cmds = []
gdb_custom_cmds_objects = []

regex_pretty_printers = gdb.printing.RegexpCollectionPrettyPrinter("Tilck")

config = None

def set_build_config(obj):
   global config
   assert(config is None)
   assert(isinstance(obj, BuildConfig))
   config = obj

def register_new_custom_gdb_cmd(cmd_class):
   gdb_custom_cmds.append(cmd_class)

def init_all_custom_cmds():

   assert(not gdb_custom_cmds_objects)

   for cmd in gdb_custom_cmds:
      gdb_custom_cmds_objects.append(cmd())

def register_tilck_regex_pp(name, regex, class_name):
   regex_pretty_printers.add_printer(name, regex, class_name)

def offset_of(gdb_type_obj, field):
   null_obj = gdb.Value(0).cast(gdb_type_obj.pointer())
   return null_obj[field].address

def container_of(elem_ptr, gdb_type_obj, mem_name):
   off = offset_of(gdb_type_obj, mem_name)
   container_addr = gdb.Value(int(elem_ptr) - int(off))
   return container_addr.cast(gdb_type_obj.pointer())

def select_field_in_list(arr, f, asString = False):

   if asString:
      return [ str(x[f]) for x in arr ]

   return [ x[f] for x in arr ]

def to_gdb_list(arr):

   if not arr:
      return "{}"

   tmp = ", ".join([ str(x) for x in arr ])
   return gdb.parse_and_eval("{{{}}}".format(tmp))

def to_gdb_list_with_field_select(arr, field, sep = ", "):
   return to_gdb_list(select_field_in_list(arr, field, True))

def fixhex16(n):
   return "0x{:04x}".format(int(n))

def fixhex32(n):
   return "0x{:08x}".format(int(n))

def fmt_type(name, gdb_val):
   addr = fixhex32(int(gdb_val.address))
   return "*({} *) {}".format(name, addr)

def get_list_elems(list_obj_ptr, container_gdb_type, list_node_container_mem):

   res = []
   curr = list_obj_ptr.cast(tt.list_node_p)['next']

   while curr != list_obj_ptr:
      obj = container_of(curr, container_gdb_type, list_node_container_mem)
      res.append(obj)
      curr = curr['next']

   return res
