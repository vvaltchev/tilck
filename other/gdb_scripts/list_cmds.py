# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
from .base_utils import *
from .tasks import *

class cmd_list_tasks(gdb.Command):

   def __init__(self):
      super(cmd_list_tasks, self).__init__("list-tasks", gdb.COMMAND_USER)

   def invoke(self, arg, from_tty):

      tasks = get_all_tasks()
      print("")

      for t in tasks:
         pi = t['pi']
         tid = str(t['tid'])
         pid = str(pi['pid'])

         pi_str = "0x{:08x}".format(int(pi)) \
            if pi['pid'] != 0 else "<kernel_process_pi>"

         gdb.execute(
            "print -symbol off -- (struct task *) 0x{:08x}"
               .format(int(t.address))
         )

         print(
            "        {{tid = {:>5}, pid = {:>5}, pi = {}}}\n".format(
               tid, pid, pi_str
            )
         )

class cmd_list_procs(gdb.Command):

   def __init__(self):
      super(cmd_list_procs, self).__init__("list-procs", gdb.COMMAND_USER)

   def invoke(self, arg, from_tty):

      tasks = get_all_tasks()
      print("")

      for t in tasks:

         pi = t['pi']
         pid = str(pi['pid'])

         if t['tid'] != pi['pid']:
            continue

         gdb.execute(
            "print -symbol off -- (struct process *) 0x{:08x}"
               .format(int(pi))
         )

         print(
            "        {{ pid = {:>5}, cmdline = '{}' }}\n".format(
               pid,
               pi['debug_cmdline'].string().rstrip()
            )
         )

# -------------------------------------------------
register_new_custom_gdb_cmd(cmd_list_tasks)
register_new_custom_gdb_cmd(cmd_list_procs)
