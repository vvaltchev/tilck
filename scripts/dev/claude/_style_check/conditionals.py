# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-function-docstring, bad-indentation
"""
The preprocessor conditionals of a C/C++ file, read from its text.

A file splits into *regions*: its top level (id 0) and every branch of
every #if/#ifdef/#ifndef group (id = the line of the directive that
opens the branch: the #if itself, an #elif or the #else). A region
holds the lines between its directive and the next directive of the
same group, minus the lines of the groups nested in it.

Per region two sets of lines matter:

  observable  every line that leaves a trace in the output of
              `gcc -E -fdirectives-only -dI` when the branch is taken:
              code, comments, #define, #include, #pragma ... -- all
              but blank lines, the conditional directives themselves
              and #error / #warning;

  code        the lines that hold something a C parser reads: neither
              a directive nor only comments and whitespace.

A region whose observable set is empty cannot be told apart from a
skipped one by looking at gcc's output, so it takes no part in the
comparisons made on top of this module.
"""

import re

from dataclasses import dataclass, field
from typing import Dict, List, Optional


COND_OPEN = ('if', 'ifdef', 'ifndef')
COND_NEXT = ('elif', 'elifdef', 'elifndef', 'else')
COND_CLOSE = ('endif',)
UNOBSERVABLE = COND_OPEN + COND_NEXT + COND_CLOSE + ('error', 'warning')

_DIRECTIVE_RE = re.compile(r'\s*#\s*([A-Za-z_]\w*)?')


@dataclass
class Region:

   id: int                       # 0 = top level, else directive line
   parent: Optional[int]         # enclosing region id (None for 0)
   first: int                    # first line of the body (1-based)
   last: int = 0                 # last line of the body (inclusive)

   # Text of the group's directives up to and including the one that
   # opens this region (e.g. ['#ifdef CLANGD', '#else']).
   conditions: List[str] = field(default_factory=list)

   observable: set = field(default_factory=set)
   code: set = field(default_factory=set)


@dataclass
class _Line:

   kind: str                     # 'blank', 'comment', 'code',
                                 # 'directive', 'cond', 'unobservable'
   directive: Optional[str] = None
   text: str = ''
   cont: bool = False            # continuation of the line above


def _classify(lines: List[str]) -> List[_Line]:
   """One entry per physical line. Tracks block comments, string and
   character literals (so a `/*` inside a string is not a comment)
   and backslash continuations (the continuation lines of a
   directive belong to it)."""

   out = []
   in_comment = False
   cont_kind = None              # kind of the logical line continued
   cont_dir = None

   for raw in lines:

      if cont_kind is not None:

         # Continuation of a directive (or of a code line).
         in_comment = _scan(raw, in_comment)[1]
         out.append(_Line(cont_kind, cont_dir, raw, cont=True))

         if not raw.rstrip().endswith('\\'):
            cont_kind = None
            cont_dir = None

         continue

      significant, in_comment_after = _scan(raw, in_comment)
      stripped = significant.strip()

      if not raw.strip():
         out.append(_Line('blank', None, raw))

      elif not stripped:
         out.append(_Line('comment', None, raw))

      elif stripped.startswith('#') and not in_comment:

         m = _DIRECTIVE_RE.match(significant)
         name = (m.group(1) or '') if m else ''

         if name in COND_OPEN + COND_NEXT + COND_CLOSE:
            kind = 'cond'
         elif name in ('error', 'warning'):
            kind = 'unobservable'
         else:
            kind = 'directive'

         out.append(_Line(kind, name, raw))

         if raw.rstrip().endswith('\\'):
            cont_kind = kind
            cont_dir = name

      else:

         out.append(_Line('code', None, raw))

         if raw.rstrip().endswith('\\') and not in_comment_after:
            cont_kind = 'code'

      in_comment = in_comment_after

   return out


def _scan(line: str, in_comment: bool):
   """Return (the line with comments blanked, whether a block comment
   is still open at its end). String and character literals are kept
   as they are."""

   out = []
   i = 0
   n = len(line)

   while i < n:

      if in_comment:

         j = line.find('*/', i)

         if j < 0:
            out.append(' ' * (n - i))
            return ''.join(out), True

         out.append(' ' * (j + 2 - i))
         i = j + 2
         in_comment = False
         continue

      c = line[i]

      if line.startswith('/*', i):
         in_comment = True
         out.append('  ')
         i += 2
         continue

      if line.startswith('//', i):
         out.append(' ' * (n - i))
         break

      if c in '"\'':

         j = i + 1

         while j < n and line[j] != c:
            j += 2 if line[j] == '\\' else 1

         out.append(line[i:j + 1])
         i = j + 1
         continue

      out.append(c)
      i += 1

   return ''.join(out), in_comment


def scan(text: str) -> List[Region]:
   """All the regions of a file, top level first, then in the order
   their directives appear."""

   lines = text.split('\n')
   classified = _classify(lines)
   top = Region(id=0, parent=None, first=1, last=len(lines))
   regions = [top]
   stack = [top]                 # open regions, innermost last
   groups = [[]]                 # directive texts of each open group

   for lineno, ln in enumerate(classified, 1):

      if ln.kind == 'cond' and not ln.cont:

         name = ln.directive
         text = ln.text.strip()

         if name in COND_OPEN:

            r = Region(id=lineno, parent=stack[-1].id, first=lineno + 1,
                       conditions=[text])
            regions.append(r)
            stack.append(r)
            groups.append([text])
            continue

         if name in COND_NEXT and len(stack) > 1:

            stack[-1].last = lineno - 1
            groups[-1].append(text)
            r = Region(id=lineno, parent=stack[-2].id, first=lineno + 1,
                       conditions=list(groups[-1]))
            regions.append(r)
            stack[-1] = r
            continue

         if name in COND_CLOSE and len(stack) > 1:

            stack[-1].last = lineno - 1
            stack.pop()
            groups.pop()
            continue

      cur = stack[-1]

      if ln.kind in ('code', 'comment', 'directive'):
         cur.observable.add(lineno)

      if ln.kind == 'code':
         cur.code.add(lineno)

   # Groups left open at the end of the file (a malformed file, or a
   # fragment): close them at the last line.
   for r in stack[1:]:
      r.last = len(lines)

   return regions


def by_id(regions: List[Region]) -> Dict[int, Region]:
   return {r.id: r for r in regions}


def ancestors(regions_by_id: Dict[int, Region], rid: int):
   """The region itself, then each enclosing region up to the top."""

   while rid is not None:
      r = regions_by_id[rid]
      yield r
      rid = r.parent
