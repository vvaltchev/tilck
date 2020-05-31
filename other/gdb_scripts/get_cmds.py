# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
from . import base_utils as bu
from . import tasks

class cmd_get_task(gdb.Command):

   def __init__(self):
      super(cmd_get_task, self).__init__("get-task", gdb.COMMAND_USER)

   def invoke(self, arg, from_tty):

      if not arg:
         print("Usage: get-task <tid>")
         return

      try:
         tid = int(arg)
      except:
         print("Usage: get-task <tid>")
         return

      task = tasks.get_task(tid)

      if not task:
         print("No such task")
         return

      gdb.execute("print *(struct task *)0x{:08x}".format(int(task)))

class cmd_get_proc(gdb.Command):

   def __init__(self):
      super(cmd_get_proc, self).__init__("get-proc", gdb.COMMAND_USER)

   def invoke(self, arg, from_tty):

      if not arg:
         print("Use: get-proc <pid>")
         return

      try:
         pid = int(arg)
      except:
         print("Usage: get-proc <pid>")
         return

      proc = tasks.get_proc(pid)

      if not proc:
         print("No such process")
         return

      gdb.execute("print *(struct process *)0x{:08x}".format(int(proc)))


# ------------------------------------------------------
bu.register_new_custom_gdb_cmd(cmd_get_task)
bu.register_new_custom_gdb_cmd(cmd_get_proc)
