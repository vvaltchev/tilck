# SPDX-License-Identifier: BSD-2-Clause

import gdb # pylint: disable=import-error
from . import base_utils as bu

fixhex16 = bu.fixhex16
fixhex32 = bu.fixhex32

class printer_regs:

   def __init__(self, val):
      self.val = val

   def to_string(self):
      return bu.fmt_type("struct x86_regs", self.val)

   def children(self):

      r = self.val
      resume_eip = gdb.parse_and_eval(
         "(void *){}".format(r["kernel_resume_eip"])
      )
      resume_eip_str = str(resume_eip)

      eip = r["eip"]
      eip_str = None

      if eip < bu.config.KERNEL_BASE_VA:
         eip_str = fixhex32(r["eip"])
      else:
         eip_str = gdb.parse_and_eval(
         "(void *){}".format(eip)
      )

      cs = r["cs"] & 0xffff
      ds = r["ds"] & 0xffff
      ss = r["ss"] & 0xffff
      es = r["es"] & 0xffff
      fs = r["fs"] & 0xffff
      gs = r["gs"] & 0xffff

      res = [
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
         ("int_num     ", r["int_num"]),
         ("err_code    ", fixhex32(r["err_code"])),
         ("eip         ", eip_str),
         ("cs          ", fixhex16(cs)),
         ("eflags      ", fixhex32(r["eflags"])),
      ]

      if resume_eip_str.find("asm_kernel_yield") == -1:
         res += [
            ("useresp     ", fixhex32(r["useresp"])),
            ("ss          ", fixhex16(ss)),
         ]
      else:
         calc_eip = gdb.parse_and_eval("(void *){}".format(r["useresp"]))
         res.append(("[true_eip]  ", calc_eip))

      return res


bu.register_tilck_regex_pp(
   'regs_t', '^(regs_t|x86_regs)$', printer_regs
)
