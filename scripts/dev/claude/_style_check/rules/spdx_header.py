# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_RAW_TEXT,
)

EXEMPT_FRAGMENTS = (
   '/include/system_headers/',
   '/3rd_party/',
   '/toolchain4/',
   '/build/',
)

# Paths considered Tilck-authored. Only files under these get an SPDX
# check; other paths (e.g. external headers vendored elsewhere) skip
# the rule.
TILCK_FRAGMENTS = (
   '/kernel/',
   '/common/',
   '/modules/',
   '/userapps/',
   '/include/tilck/',
   '/boot/',
   '/tests/self/',
   '/tests/system/',
)

EXPECTED_C_HEADER = '/* SPDX-License-Identifier: BSD-2-Clause */'


class SpdxHeader(Rule):

   id = 'spdx_header'
   description = (
      'Tilck-authored files must start with '
      '/* SPDX-License-Identifier: BSD-2-Clause */ on line 1'
   )
   layers = LAYER_RAW_TEXT

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      path_s = str(ctx.file_path)

      if any(frag in path_s for frag in EXEMPT_FRAGMENTS):
         return []

      if not any(frag in path_s for frag in TILCK_FRAGMENTS):
         return []

      if not ctx.lines:
         return []

      line1 = ctx.lines[0]

      if line1.strip() == EXPECTED_C_HEADER:
         return []

      return [Diagnostic(
         file=path_s,
         line=1,
         col=1,
         end_line=1,
         end_col=len(line1) + 1,
         rule=self.id,
         severity=self.severity,
         message=('line 1 must be: ' + EXPECTED_C_HEADER),
         snippet=line1,
      )]


RULE = SpdxHeader()
