# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
from . import base_utils as bu
from . import tilck_types as tt
from . import tasks

class cmd_get_task(gdb.Command):

   def __init__(self):
      super(cmd_get_task, self).__init__("get-task", gdb.COMMAND_USER)

   def show_usage(self):
      print("Usage: get-task <tid>")

   def invoke(self, arg, from_tty):

      try:
         tid = int(arg)
      except:
         self.show_usage()
         return

      task = tasks.get_task(tid)

      if not task:
         print("No such task")
         return

      gdb.execute("print *(struct task *){}".format(int(task)))

class cmd_get_proc(gdb.Command):

   def __init__(self):
      super(cmd_get_proc, self).__init__("get-proc", gdb.COMMAND_USER)

   def show_usage(self):
      print("Use: get-proc <pid>")

   def invoke(self, arg, from_tty):

      try:
         pid = int(arg)
      except:
         self.show_usage()
         return

      proc = tasks.get_proc(pid)

      if not proc:
         print("No such process")
         return

      gdb.execute("print *(struct process *){}".format(int(proc)))

class cmd_get_handle2(gdb.Command):

   def __init__(self):
      super(cmd_get_handle2, self).__init__("get-handle2", gdb.COMMAND_USER)

   def show_usage(self):
      print("Use: get-handle2 <pid> <handle_number>")

   def invoke(self, arg, from_tty):

      try:

         pid, hn = arg.split(" ")
         pid = int(pid)
         hn = int(hn)

      except:
         self.show_usage()
         return

      proc = tasks.get_proc(pid)

      if not proc:
         print("No such process")
         return

      handle = tasks.get_handle(proc, hn)

      if not handle:
         print("No such handle")
         return

      gdb.execute("print *(struct fs_handle_base *){}".format(int(handle)))

class cmd_get_handle(gdb.Command):

   def __init__(self):
      super(cmd_get_handle, self).__init__("get-handle", gdb.COMMAND_USER)

   def show_usage(self):
      print("Use: get-handle <handle_number> # assuming the current process")

   def invoke(self, arg, from_tty):

      try:
         hn = int(arg)
      except:
         self.show_usage()
         return

      curr = gdb.parse_and_eval("__current->pi")
      handle = tasks.get_handle(curr, hn)

      if not handle:
         print("No such handle")
         return

      gdb.execute("print *(struct fs_handle_base *){}".format(int(handle)))

class cmd_get_curr(gdb.Command):

   def __init__(self):
      super(cmd_get_curr, self).__init__("get-curr", gdb.COMMAND_USER)

   def invoke(self, arg, from_tty):
      gdb.execute("print *__current")

class cmd_get_currp(gdb.Command):

   def __init__(self):
      super(cmd_get_currp, self).__init__("get-currp", gdb.COMMAND_USER)

   def invoke(self, arg, from_tty):
      gdb.execute("print *__current->pi")


# ------------------------------------------------------
bu.register_new_custom_gdb_cmd(cmd_get_task)
bu.register_new_custom_gdb_cmd(cmd_get_proc)
bu.register_new_custom_gdb_cmd(cmd_get_handle2)
bu.register_new_custom_gdb_cmd(cmd_get_handle)
bu.register_new_custom_gdb_cmd(cmd_get_curr)
bu.register_new_custom_gdb_cmd(cmd_get_currp)
