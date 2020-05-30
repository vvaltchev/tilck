# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
import sys
import re

# Constants coming from CMake (this file gets pre-processed by CMake)
OTHER_DIR = "@CMAKE_SOURCE_DIR@/other"
# ---

sys.path.append(OTHER_DIR)
from gdb_scripts.base_utils import *
from gdb_scripts.tasks import *

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

      task = get_task(tid)

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

      proc = get_proc(pid)

      if not proc:
         print("No such process")
         return

      gdb.execute("print *(struct process *)0x{:08x}".format(int(proc)))


class printer_struct_process:

   def __init__(self, val):
      self.val = val

   def to_string(self):

      val = self.val
      children_tasks = get_children_list(val)

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

def build_pretty_printers():
   pp = gdb.printing.RegexpCollectionPrettyPrinter("Tilck")
   pp.add_printer('process', '^process$', printer_struct_process)
   return pp

def build_gdb_cmds():
   cmd_list_tasks()
   cmd_list_procs()
   cmd_get_proc()
   cmd_get_task()

# ------------------ MAIN ------------------

build_gdb_cmds()

gdb.printing.register_pretty_printer(
   gdb.current_objfile(),
   build_pretty_printers()
)
