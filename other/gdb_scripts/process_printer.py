# SPDX-License-Identifier: BSD-2-Clause

import gdb # pylint: disable=import-error
from . import base_utils as bu
from . import tilck_types as tt
from . import tasks

class printer_struct_process:

   def __init__(self, val):
      self.val = val

   def children(self):

      proc = self.val

      # Note: it doesn't make sense to take t['pi']['pid'] because children
      # tasks are process' main threads, having always tid == pid.
      children_arr = bu.to_gdb_list_with_field_select(
         tasks.get_children_list(proc), "tid"
      )

      handles_arr = bu.to_gdb_list(tasks.get_handles(proc))
      debug_cmdline = proc['debug_cmdline']

      if debug_cmdline:
         cmdline = "\"{}\"".format(debug_cmdline.string().rstrip())
      else:
         cmdline = "(null)"

      cwd = proc['str_cwd'].string()
      mi = "(null)"

      if proc['mi']:
         mi = proc['mi'].dereference()

      return [
         ("pid                ", proc['pid']),
         ("cmdline            ", cmdline),
         ("parent_pid         ", proc['parent_pid']),
         ("pgid               ", proc['pgid']),
         ("sid                ", proc['sid']),
         ("brk                ", proc['brk']),
         ("initial_brk        ", proc['initial_brk']),
         ("children           ", children_arr),
         ("did_call_execve    ", proc['did_call_execve']),
         ("vforked            ", proc['vforked']),
         ("inherited_mmap_heap", proc['inherited_mmap_heap']),
         ("str_cwd            ", "\"{}\"".format(cwd)),
         ("handles            ", handles_arr),
         ("mi                 ", mi),
      ]

   def to_string(self):
      return bu.fmt_type("struct process", self.val)

bu.register_tilck_regex_pp(
   'process', '^process$', printer_struct_process
)
