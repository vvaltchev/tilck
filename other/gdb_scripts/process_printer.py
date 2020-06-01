# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
from . import base_utils as bu
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
      cmdline = proc['debug_cmdline'].string().rstrip()
      cwd = proc['str_cwd'].string()

      return [
         ("pid                ", proc['pid']),
         ("cmdline            ", "\"{}\"".format(cmdline)),
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
      ]

   def to_string(self):

      proc = self.val
      return "(struct process *) {}".format(bu.fixhex32(int(proc.address)))


bu.register_tilck_regex_pp(
   'process', '^process$', printer_struct_process
)
