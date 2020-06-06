# SPDX-License-Identifier: BSD-2-Clause

import gdb # pylint: disable=import-error
from . import base_utils as bu
from . import tilck_types as tt

def get_all_tasks():

   tasks = []
   root = gdb.lookup_symbol("tree_by_tid_root")[0]

   def walk(tasks_list, task):

      if not task:
         return

      tasks_list.append(task)

      left = task['tree_by_tid_node']['left_obj'].cast(tt.task_p)
      right = task['tree_by_tid_node']['right_obj'].cast(tt.task_p)

      walk(tasks_list, left)
      walk(tasks_list, right)

   walk(tasks, root.value())
   tasks = sorted(tasks, key = lambda t: int(t['tid']))
   return tasks

def get_children_list(proc):

   return bu.get_list_elems(
      proc['children'].address,    # pointer to list object
      tt.task,                # container type
      "siblings_node"              # container's list node member name
   )

def get_task(tid):

   tasks = get_all_tasks()

   for t in tasks:

      task_tid = int(t['tid'])

      if task_tid == tid:
         return t

   return None

def get_proc(pid):

   tasks = get_all_tasks()

   for t in tasks:

      pi = t['pi']

      if pi['pid'] == pid:
         return pi

   return None

def get_handles(proc):

   handles_list = []
   handles = proc['handles']

   for i in range(bu.config.MAX_HANDLES):
      if handles[i]:
         handles_list.append(i)

   return handles_list

def get_handle(proc, n):

   if n not in range(0, bu.config.MAX_HANDLES):
      return None

   return proc['handles'][n].cast(tt.fs_handle_base_p)

def get_handle_num(proc, handle_obj_ptr):

   handles = proc['handles']

   for i in range(bu.config.MAX_HANDLES):

      if handles[i] == handle_obj_ptr:
         return i

   return None
