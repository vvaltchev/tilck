# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-module-docstring, missing-class-docstring
# pylint: disable=too-few-public-methods, missing-function-docstring

from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

# Severity levels
SEVERITY_ERROR = 'error'
SEVERITY_WARNING = 'warning'

# Layer tags
LAYER_RAW_TEXT = 'R'
LAYER_TOKENS = 'T'
LAYER_STRUCTURAL = 'S'
LAYER_COMMENTS = 'C'

# Default per-rule prettiness penalties (signed doubles, unbounded).
# Negative values are "ugly"; positive values mean "actively pretty"
# or "legitimate override" (e.g. SCORE_CONTEXT_OK, used by context-
# sensitive rules to forgive a violation when justified by
# surroundings).
#
# Tier semantics (see docs/rule-classification.md for the audit):
#
#   HARD_RULE     -10.0  error    -- defect (build/semantic/policy)
#   STRONG_PREF    -3.0  warning  -- "hard rule" in casual prefs but
#                                    cosmetic in nature
#   MEDIUM_PREF    -1.5  warning  -- "ugly", "avoid", conventions
#   SOFT           -0.5  warning  -- mild preference (hex case, etc.)
#   NUDGE          -0.2  warning  -- low-threshold suggestion
#   CONTEXT_OK     +0.5  --       -- forgiveness modifier when a
#                                    soft rule is legitimately
#                                    overridden by context
SCORE_HARD_RULE   = -10.0
SCORE_STRONG_PREF = -3.0
SCORE_MEDIUM_PREF = -1.5
SCORE_SOFT        = -0.5
SCORE_NUDGE       = -0.2
SCORE_CONTEXT_OK  = +0.5

# Prettiness cost tiers for gradient rules. Each scoreable line
# starts at 1.0; gradient diagnostics subtract their cost from the
# affected line's prettiness. Individual line scores CAN go negative
# (no per-line floor). Function prettiness = mean of line scores --
# also UNCLAMPED, so an ugly function can score well below 0.
#
# Hard-failure escalation: a gradient diagnostic whose cost meets or
# exceeds STATEMENT_HARD_FAIL_THRESHOLD is treated as a hard failure
# (red in the reporter, factored into the 'broken' verdict). The
# rest of the function's prettiness is still computed -- useful for
# spotting whether a single bad statement is the only problem or the
# function is broadly ugly.
COST_MINOR      = 0.10   # barely noticeable imperfection
COST_MILD       = 0.20   # acceptable tradeoff for line length
COST_MODERATE   = 0.35   # clearly suboptimal, prefer alternative
COST_SIGNIFICANT = 0.50  # ugly but technically valid
COST_FULL       = 1.00   # drops a clean statement to the minimum
                         # "still valid" score of 0.0; the next bit
                         # past this enters hard-failure territory
STATEMENT_HARD_FAIL_THRESHOLD = 3.0


@dataclass
class Fix:
   start_line: int        # 1-based, first line of the replaced range
   end_line: int           # 1-based, last line (inclusive)
   new_lines: list         # replacement lines (list of str, no trailing \n)
   description: str = ''


@dataclass
class Diagnostic:
   file: str
   line: int
   col: int
   end_line: int
   end_col: int
   rule: str
   severity: str
   message: str
   snippet: Optional[str] = None
   score: float = 0.0
   suggestion: Optional[str] = None
   fixes: list = field(default_factory=list)  # List[Fix]
   is_gradient: bool = False       # gradient: affects prettiness only
   prettiness_cost: float = 0.0    # subtracted from the affected
                                   # line; may exceed 1.0 in which
                                   # case the line score goes
                                   # negative -- intentional

   @property
   def is_hard_failure(self) -> bool:
      """True when the diagnostic should be treated as a hard
      failure: either a hard-rule violation (severity='error'), or a
      gradient with cost >= STATEMENT_HARD_FAIL_THRESHOLD (which
      indicates the locality is so catastrophically ugly the rule
      escalates it on its own)."""

      if self.severity == 'error':
         return True

      if self.is_gradient and \
         self.prettiness_cost >= STATEMENT_HARD_FAIL_THRESHOLD:
         return True

      return False


@dataclass
class CheckContext:

   # Always available
   file_path: Path
   source_bytes: bytes
   source_text: str
   lines: List[str]
   is_header: bool
   is_cpp: bool

   # Number of space characters per indentation level in this code
   # base. Drives the width-sensitive rules (tab-expansion in
   # `indent_3sp`, the case-label step in `switch_case_indent`).
   # Defaults to 3 (Tilck style); the CLI `--indent N` overrides it.
   indent: int = 3

   # `.c.h` "implementation-include" files: included exactly once by
   # a specific .c file. Not standalone headers, so they don't need
   # `#pragma once` (and several other header-only rules don't apply).
   is_c_dot_h: bool = False

   # Multi-include / X-macro headers tagged with `/* style_check:
   # multi-include */` near the top. Re-includable by design:
   # exempt from `pragma_once` and similar single-include rules.
   is_multi_include: bool = False

   # libclang artefacts -- may be None if parse failed or rule didn't need them
   tu: Optional[object] = None
   extents: list = field(default_factory=list)

   # Comment ranges from the raw-text scanner: list of CommentRange tuples
   comments: list = field(default_factory=list)


# Magic marker recognised in the file's first ~10 lines to opt a
# header into multi-include mode. Either spelling is accepted.
MULTI_INCLUDE_MARKERS = (
   '/* style_check: multi-include */',
   '/* style_check: re-includable */',
)


def detect_multi_include(lines: List[str]) -> bool:
   """Return True iff one of the marker comments is present near
   the top of the file. Looking at the first 10 lines covers the
   SPDX header + optional file-comment + the marker."""

   for line in lines[:10]:

      s = line.strip()

      for marker in MULTI_INCLUDE_MARKERS:

         if s == marker:
            return True

   return False


def detect_c_dot_h(path: Path) -> bool:
   """Return True iff the path ends with `.c.h` -- Tilck's
   convention for implementation-include files."""

   return str(path).endswith('.c.h')


class Rule:

   # Subclasses override these
   id: str = ''
   description: str = ''
   layers: str = ''
   severity: str = SEVERITY_ERROR

   # Default prettiness penalty applied per diagnostic. Rules may
   # override per-diagnostic by setting `score` explicitly.
   default_score: float = SCORE_HARD_RULE

   # Capability requirements
   needs_tu: bool = False
   needs_comments: bool = False

   # File-type filter: set of file extensions this rule applies to.
   # None means: all C/C++ source files (.c, .h, .cpp, .hpp).
   # Use {'.c'} or {'.c', '.h'} for C-only rules.
   applies_to: Optional[set] = None  # e.g. {'.c'} or {'.h'}

   ALL_EXTENSIONS = {'.c', '.h', '.cpp', '.hpp'}

   def applies_to_file(self, path: Path) -> bool:
      if self.applies_to is None:
         return path.suffix in self.ALL_EXTENSIONS
      return path.suffix in self.applies_to

   def check(self, ctx: CheckContext) -> List[Diagnostic]:
      raise NotImplementedError(self.id)

   def explain(self) -> str:
      return self.description
