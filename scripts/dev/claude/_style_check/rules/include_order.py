# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import re

from typing import List

from .base import (
   Rule,
   Diagnostic,
   CheckContext,
   LAYER_RAW_TEXT,
   SCORE_HARD_RULE,
)

# Q13 hard rules:
#   - `tilck_gen_headers/...` MUST be the first include (it carries
#     generated config defines other headers depend on).
#   - Includes MUST be grouped by subtree (gen_headers / common /
#     kernel / mods / 3rd_party / system). Strict alphabetical
#     ordering that interleaves subtrees is forbidden.

# Captures: (delim, path) where delim is `<` or `"`. ANCHORED at
# column 1 (no leading whitespace) -- the file's main include
# block always starts there. Indented `#include` directives are
# inside a special context (X-macro setup, conditional compilation,
# etc.) and the ordering rule does not apply to them.
#
# Example (kernel/cmdline.c) of the intentionally indented form:
#
#    #define DEFINE_KOPT(name, alias, type, default) \
#       type kopt_##name = default;
#       #include <tilck/common/cmdline_opts.h>
#    #undef DEFINE_KOPT
_INCLUDE_PAT = re.compile(
   r'^#\s*include\s*(?P<delim>[<"])(?P<path>[^>"]+)[>"]'
)

# Subtree-of-interest for the grouping check. Returns None for
# includes the rule does not enforce ordering on (local-quoted
# includes, system-style `<header.h>`, vendored 3rd-party paths).
def _subtree_of(path: str, delim: str):

   if delim == '"':
      return None   # local quoted include -- not subject to grouping

   if path.startswith('tilck_gen_headers/'):
      return 'gen_headers'

   if path.startswith('tilck/common/'):
      return 'common'

   if path.startswith('tilck/kernel/'):
      return 'kernel'

   if path.startswith('tilck/mods/'):
      return 'mods'

   if path.startswith('3rd_party/'):
      return 'third_party'

   return None   # system header or other -- not enforced


class IncludeOrder(Rule):

   id = 'include_order'
   description = (
      '`#include` ordering: tilck_gen_headers/ must be first; '
      'includes must be grouped by subtree, not interleaved; strict '
      'alphabetical ordering across subtrees is forbidden. Q13 hard '
      'rule.'
   )
   # The Q13 rule was articulated for .c files (top-of-file
   # #include block ordering). Headers have their own conventions
   # (e.g. include/tilck/common/atomics.h includes basic_defs.h
   # before tilck_gen_headers/config_debug.h) and are not covered
   # by this rule.
   layers = LAYER_RAW_TEXT
   applies_to = {'.c'}
   default_score = SCORE_HARD_RULE

   def check(self, ctx: CheckContext) -> List[Diagnostic]:

      # Skip external / vendored / system header files where the
      # ordering rule does not apply.
      path_s = str(ctx.file_path)
      exempt_fragments = ('/include/system_headers/', '/3rd_party/',
                          '/toolchain4/', '/build/')

      if any(frag in path_s for frag in exempt_fragments):
         return []

      # Parse all #include directives with their paths and subtrees.
      # Only tilck-tracked subtrees participate in the grouping check;
      # local quoted includes and system headers are ignored.
      includes = []   # list of (line_no, path, subtree, raw_line)

      for i, line in enumerate(ctx.lines, start=1):

         m = _INCLUDE_PAT.match(line)

         if not m:
            continue

         path = m.group('path')
         delim = m.group('delim')
         subtree = _subtree_of(path, delim)

         if subtree is None:
            continue   # not subject to ordering rules

         includes.append((i, path, subtree, line))

      if not includes:
         return []

      out = []

      # Rule 1: if any include is gen_headers, the FIRST include
      # must be a gen_headers one. Files that don't need
      # tilck_gen_headers don't have to include it; but if they do,
      # it leads.
      first_gen_idx = next(
         (idx for idx, inc in enumerate(includes)
          if inc[2] == 'gen_headers'),
         -1
      )

      if first_gen_idx > 0:

         line_no, path, _subtree, raw = includes[first_gen_idx]

         out.append(Diagnostic(
            file=str(ctx.file_path),
            line=line_no,
            col=1,
            end_line=line_no,
            end_col=len(raw) + 1,
            rule=self.id,
            severity=self.severity,
            message=('tilck_gen_headers/ include `{}` must be the '
                     'FIRST #include; got `{}` first instead').format(
               path, includes[0][1]
            ),
            snippet=raw.strip(),
         ))

      # Rule 2: subtrees must not interleave. Walk the sequence; any
      # subtree that re-appears after a different subtree intervened
      # is an interleaving violation.
      seen = set()
      current = None

      for line_no, path, subtree, raw in includes:

         if subtree == current:
            continue

         if subtree in seen:

            out.append(Diagnostic(
               file=str(ctx.file_path),
               line=line_no,
               col=1,
               end_line=line_no,
               end_col=len(raw) + 1,
               rule=self.id,
               severity=self.severity,
               message=('#include `{}` (subtree `{}`) appears after a '
                        'different subtree -- includes must be grouped '
                        'by subtree, not interleaved').format(
                  path, subtree
               ),
               snippet=raw.strip(),
            ))

         else:
            seen.add(subtree)

         current = subtree

      return out


RULE = IncludeOrder()
