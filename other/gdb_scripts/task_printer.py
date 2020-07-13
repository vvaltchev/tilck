# SPDX-License-Identifier: BSD-2-Clause

import gdb # pylint: disable=import-error
from . import base_utils as bu
from . import tasks

class printer_struct_task:

   def __init__(self, val):
      self.val = val

   def children(self):

      task = self.val
      pi = task['pi']
      what_str = "n/a"
      state_regs = "(null)"

      if pi['pid'] != 0:
         pi_str = "(struct process *) {}".format(bu.fixhex32(int(pi)))
      else:
         pi_str = "<kernel_process_pi>"
         what_str = task['what']

      if task['state_regs']:
         state_regs = task['state_regs'].dereference()

      return [
         ("tid                 ", task["tid"]),
         ("pi                  ", pi_str),
         ("state               ", task["state"]),
         ("what                ", what_str),
         ("is_main_thread      ", task["is_main_thread"]),
         ("running_in_kernel   ", task['running_in_kernel']),
         ("stopped             ", task['stopped']),
         ("was_stopped         ", task['was_stopped']),
         ("vfork_stopped       ", task['vfork_stopped']),
         ("traced              ", task['traced']),
         ("kernel_stack        ", task['kernel_stack']),
         ("args_copybuf        ", task['args_copybuf']),
         ("io_copybuf          ", task['io_copybuf']),
         ("wstatus             ", task['wstatus']),
         ("timeslice_ticks     ", task['ticks']['timeslice']),
         ("total_ticks         ", task['ticks']['total']),
         ("total_kernel_ticks  ", task['ticks']['total_kernel']),
         ("ticks_before_wake_up", task['ticks_before_wake_up']),
         ("wobj                ", task['wobj']),
         ("state_regs          ", state_regs),
      ]

   def to_string(self):
      return bu.fmt_type("struct task", self.val)

bu.register_tilck_regex_pp(
   'task', '^task$', printer_struct_task
)
