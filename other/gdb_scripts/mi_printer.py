# SPDX-License-Identifier: BSD-2-Clause

import gdb # pylint: disable=import-error
from . import base_utils as bu
from . import tilck_types as tt
from . import tasks

class printer_user_mapping:

   def __init__(self, val):
      self.val = val

   def to_string(self):
      return "mapping"

   def children(self):

      m = self.val
      pi = m["pi"]
      h = m["h"]
      handle_str = "(null)"

      if h:
         handle_n = tasks.get_handle_num(pi, h)

         if handle_n:
            handle_str = str(handle_n)
         else:
            handle_str = "<unknown handle at: {}>".format(bu.fixhex32(int(h)))

      return [
         ("pid       ", pi['pid']),
         ("handle    ", handle_str),
         ("prot      ", m["prot"]),
         ("offset    ", hex(m["off"])),
         ("va_begin  ", bu.fixhex32(m["vaddr"])),
         ("va_end    ", bu.fixhex32(m["vaddr"] + m["len"])),
         ("length    ", m["len"]),
      ]



class printer_mappings_info:

   def __init__(self, val):
      self.val = val

   def to_string(self):
      return bu.fmt_type("struct mappings_info", self.val)

   def children(self):

      mi = self.val
      res = []
      mappings = bu.get_list_elems(
         mi["mappings"].address,
         tt.user_mapping,
         "pi_node"
      )

      for i, e in enumerate(mappings):
        res.append(("[{}]".format(i), e.dereference()))

      return res

bu.register_tilck_regex_pp(
   'mappings_info', '^mappings_info$', printer_mappings_info
)

bu.register_tilck_regex_pp(
   'user_mapping', '^user_mapping$', printer_user_mapping
)
