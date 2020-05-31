# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
from . import base_utils as bu
from . import tasks

class printer_struct_process:

   def __init__(self, val):
      self.val = val

   def to_string(self):

      proc = self.val

      # Note: it doesn't make sense to take t['pi']['pid'] because children
      # tasks are process' main threads, having always tid == pid.
      children_str = bu.joined_str_list_with_field_select(
         tasks.get_children_list(proc), "tid"
      )

      handles_list_str = bu.joined_str_list_with_field_select(
         tasks.get_handles(proc)
      )

      res = """(struct process *) 0x{:08x} {{
   pid                 = {}
   cmdline             = '{}'
   parent_pid          = {}
   pgid                = {}
   sid                 = {}
   brk                 = {}
   initial_brk         = {}
   children            = [ {} ]
   did_call_execve     = {}
   vforked             = {}
   inherited_mmap_heap = {}
   str_cwd             = '{}'
   handles             = [ {} ]
}}
"""

      return res.format(
         int(proc.address),
         proc['pid'],
         proc['debug_cmdline'].string().rstrip(),
         proc['parent_pid'],
         proc['pgid'],
         proc['sid'],
         proc['brk'],
         proc['initial_brk'],
         children_str,
         proc['did_call_execve'],
         proc['vforked'],
         proc['inherited_mmap_heap'],
         proc['str_cwd'].string(),
         handles_list_str
      )

bu.register_tilck_regex_pp('process', '^process$', printer_struct_process)
