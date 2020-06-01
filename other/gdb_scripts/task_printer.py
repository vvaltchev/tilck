# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
from . import base_utils as bu
from . import tasks

WOBJ_NONE = gdb.parse_and_eval("(int)WOBJ_NONE")
WOBJ_TASK = gdb.parse_and_eval("(int)WOBJ_TASK")
WOBJ_MWO_WAITER = gdb.parse_and_eval("(int)WOBJ_MWO_WAITER")

multi_obj_waiter = gdb.lookup_type("struct multi_obj_waiter")
multi_obj_waiter_p = multi_obj_waiter.pointer()

def format_mwobj_elem(elem):
   return "{{type = {}, ptr = {}}}".format(
      elem['type'],
      elem['wobj']['__ptr']
   )

class printer_wait_obj:

   def __init__(self, val):
      self.val = val

   def to_string(self):

      wobj = self.val
      body = ""

      if wobj['type'] == WOBJ_TASK:

         tidval = wobj['__data']

         if tidval < -1:
            tidval = "<any child with pgid = {}>".format(-tidval)
         elif tidval == -1:
            tidval = "<any child>"
         elif tidval == 0:
            tidval = "<any child with same pgid>"

         body = "tid     = {}".format(tidval)

      elif wobj['type'] == WOBJ_MWO_WAITER:

         mwobj = wobj['__ptr'].cast(multi_obj_waiter_p)

         count = mwobj['count']
         elems = mwobj['elems']
         arr = []

         for i in range(count):
            arr.append(format_mwobj_elem(elems[i]))

         body =  "ptr     = "
         body += "(struct multi_obj_waiter *) 0x{:08x} ".format(int(mwobj))
         body += "[{}]".format(", ".join(arr))

      else:

         body = "ptr      = {}".format(wobj['__ptr'])

      res = """(struct wait_obj *) 0x{:08x} {{
      type    = {}
      extra   = {}
      {}
   }}"""

      return res.format(
         int(wobj.address),
         wobj['type'],
         wobj['extra'],
         body,
      )

class printer_struct_task:

   def __init__(self, val):
      self.val = val

   def to_string(self):

      task = self.val
      pi = task['pi']
      what_str = "n/a"

      if pi['pid'] != 0:
         pi_str = "(struct process *) 0x{:08x}".format(int(pi))
      else:
         pi_str = "<kernel_process_pi>"
         what_str = task['what']

      res = """(struct task *) 0x{:08x} {{
   tid                  = {}
   pi                   = {}
   state                = {}
   what                 = {}
   is_main_thread       = {}
   running_in_kernel    = {}
   stopped              = {}
   was_stopped          = {}
   vfork_stopped        = {}
   traced               = {}
   kernel_stack         = {}
   args_copybuf         = {}
   io_copybuf           = {}
   wstatus              = {}
   time_slot_ticks      = {}
   total_ticks          = {}
   total_kernel_ticks   = {}
   ticks_before_wake_up = {}
   wobj                 = {}
   state_regs           = {}
}}"""
      return res.format(
         int(task.address),
         task['tid'],
         pi_str,
         task['state'],
         what_str,
         task['is_main_thread'],
         task['running_in_kernel'],
         task['stopped'],
         task['was_stopped'],
         task['vfork_stopped'],
         task['traced'],
         task['kernel_stack'],
         task['args_copybuf'],
         task['io_copybuf'],
         task['wstatus'],
         task['time_slot_ticks'],
         task['total_ticks'],
         task['total_kernel_ticks'],
         task['ticks_before_wake_up'],
         task['wobj'],
         task['state_regs'].dereference(),
      )

bu.register_tilck_regex_pp(
   'task', '^task$', printer_struct_task
)

bu.register_tilck_regex_pp(
   'wait_obj', '^wait_obj$', printer_wait_obj
)
