# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-module-docstring

from . import cols_80
from . import pragma_once
from . import sizeof_parens
from . import static_fn_def_type_own_line
from . import comment_block_multiline_format

# M2a -- raw-text and token rules (no libclang TU needed)
from . import indent_3sp
from . import trailing_ws
from . import spdx_header
from . import hex_literal_lowercase
from . import no_void_cast_discard

# M2b -- structural rules via libclang + raw text
from . import void_arglist
from . import no_trailing_enum_comma
from . import one_stmt_per_line

# M2c -- control flow + brace placement
from . import else_same_line_as_brace
from . import fn_body_brace_own_line

# goto_label_flush_left was prototyped but NOT registered: the "flush
# left" rule applies only to function-scope cleanup labels (kernel/
# fork.c:175-177 style), not to labels nested in deeper block scopes
# (userapps/devshell/devshell.c:286 has an indented label inside a
# while loop). Distinguishing the two requires tracking scope depth.
# Re-enable in v2 with proper scope analysis.

# `null_check_no_null` (if (ptr != NULL) -> if (ptr)) is intentionally
# NOT registered: corpus inspection shows ASSERT(ptr != NULL),
# LIKELY(ptr != NULL), and plain if (ptr != NULL) are all used by
# hand in kernel/poll.c, fork.c, execve.c, signal.c, etc.
# docs/contributing.md says "generally" (not "always"), and the
# user's actual style is permissive on this point. Revisit in v2 if
# we find a narrower, capturable form.

ALL_RULES = [
   cols_80.RULE,
   pragma_once.RULE,
   sizeof_parens.RULE,
   static_fn_def_type_own_line.RULE,
   comment_block_multiline_format.RULE,
   indent_3sp.RULE,
   trailing_ws.RULE,
   spdx_header.RULE,
   hex_literal_lowercase.RULE,
   no_void_cast_discard.RULE,
   void_arglist.RULE,
   no_trailing_enum_comma.RULE,
   one_stmt_per_line.RULE,
   else_same_line_as_brace.RULE,
   fn_body_brace_own_line.RULE,
]

RULES_BY_ID = { r.id: r for r in ALL_RULES }
