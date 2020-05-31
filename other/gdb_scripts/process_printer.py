# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
from . import base_utils as bu
from . import tasks

class printer_struct_process:

   def __init__(self, val):
      self.val = val

   def to_string(self):

      val = self.val
      children_tasks = tasks.get_children_list(val)

      # Note: it doesn't make sense to take t['pi']['pid'] because children
      # tasks are process' main threads, having always tid == pid.
      children_pids_str = [str(t['tid']) for t in children_tasks]
      children_str = ", ".join(children_pids_str)

      res = """(struct process *) 0x{:08x} {{
   pid                 = {}
   cmdline             = '{}'
   parent_pid          = {}
   pgid                = {}
   sid                 = {}
   brk                 = {}
   initial_brk         = {}
   children            = {{ {} }}
   did_call_execve     = {}
   vforked             = {}
   inherited_mmap_heap = {}
   str_cwd             = '{}'
}}
"""

      return res.format(
         int(val.address),
         val['pid'], val['debug_cmdline'].string().rstrip(),
         val['parent_pid'],
         val['pgid'],
         val['sid'],
         val['brk'],
         val['initial_brk'],
         children_str,
         val['did_call_execve'],
         val['vforked'],
         val['inherited_mmap_heap'],
         val['str_cwd'].string(),
      )

bu.register_tilck_regex_pp('process', '^process$', printer_struct_process)
