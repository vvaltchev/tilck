# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error
from . import base_utils as bu

fixhex16 = bu.fixhex16
fixhex32 = bu.fixhex32

class printer_regs:

   def __init__(self, val):
      self.val = val

   def to_string(self):
      r = self.val
      return "(struct x86_regs *) {}".format(fixhex32(int(r.address)))

   def children(self):

      r = self.val
      resume_eip = gdb.parse_and_eval(
         "(void *){}".format(r["kernel_resume_eip"])
      )

      cs = r["cs"] & 0xffff
      ds = r["ds"] & 0xffff
      ss = r["ss"] & 0xffff
      es = r["es"] & 0xffff
      fs = r["fs"] & 0xffff
      gs = r["gs"] & 0xffff

      return [
         ("resume_eip  ", resume_eip),
         ("custom_flags", fixhex32(r["custom_flags"])),
         ("gs          ", fixhex16(gs)),
         ("es          ", fixhex16(es)),
         ("ds          ", fixhex16(ds)),
         ("fs          ", fixhex16(fs)),
         ("edi         ", fixhex32(r["edi"])),
         ("esi         ", fixhex32(r["esi"])),
         ("ebp         ", fixhex32(r["ebp"])),
         ("esp         ", fixhex32(r["esp"])),
         ("ebx         ", fixhex32(r["ebx"])),
         ("edx         ", fixhex32(r["edx"])),
         ("ecx         ", fixhex32(r["ecx"])),
         ("eax         ", fixhex32(r["eax"])),
         ("eip         ", fixhex32(r["eip"])),
         ("cs          ", fixhex16(cs)),
         ("eflags      ", fixhex32(r["eflags"])),
         ("useresp     ", fixhex32(r["useresp"])),
         ("ss          ", fixhex16(ss)),
      ]


bu.register_tilck_regex_pp(
   'regs_t', '^(regs_t|x86_regs)$', printer_regs
)
