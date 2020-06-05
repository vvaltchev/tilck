# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
from . import base_utils as bu

class printer_fs_handle_base:

   def __init__(self, val):
      self.val = val

   def to_string(self):
      r = self.val
      return "(struct fs_handle_base *) {}".format(bu.fixhex32(int(r.address)))

   def children(self):

      h = self.val

      return [
         ("pi        ", h['pi']['pid']),
         ("fs        ", h['fs']),
         ("fs_type   ", h['fs']['fs_type_name'].string()),
         ("fd_flags  ", h['fd_flags']),
         ("fl_flags  ", h['fl_flags']),
         ("spec_flags", h['spec_flags']),
         ("pos       ", h['pos']),
      ]

bu.register_tilck_regex_pp(
   'fs_handle_base', '^fs_handle_base$', printer_fs_handle_base
)

