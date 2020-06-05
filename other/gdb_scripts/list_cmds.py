# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
from . import base_utils as bu
from . import tasks

class cmd_list_tasks(gdb.Command):

   def __init__(self):
      super(cmd_list_tasks, self).__init__("list-tasks", gdb.COMMAND_USER)

   def invoke(self, arg, from_tty):

      tasks_list = tasks.get_all_tasks()
      print("")

      for t in tasks_list:
         pi = t['pi']
         tid = str(t['tid'])
         pid = str(pi['pid'])

         gdb.execute(
            "print (struct task *) {}".format(bu.fixhex32(int(t)))
         )

         print(
            "        "
            "{{tid = {:>5}, pid = {:>5}, state = {}}}\n".format(
               tid, pid, t['state']
            )
         )

class cmd_list_procs(gdb.Command):

   def __init__(self):
      super(cmd_list_procs, self).__init__("list-procs", gdb.COMMAND_USER)

   def invoke(self, arg, from_tty):

      tasks_list = tasks.get_all_tasks()
      print("")

      for t in tasks_list:

         pi = t['pi']
         pid = str(pi['pid'])

         if t['tid'] != pi['pid']:
            continue

         gdb.execute(
            "print (struct process *) {}".format(bu.fixhex32(int(pi)))
         )

         print(
            "        {{ pid = {:>5}, cmdline = '{}' }}\n".format(
               pid,
               pi['debug_cmdline'].string().rstrip()
            )
         )

# -------------------------------------------------
bu.register_new_custom_gdb_cmd(cmd_list_tasks)
bu.register_new_custom_gdb_cmd(cmd_list_procs)
