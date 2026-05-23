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

# v2 (post Q1-Q50) -- new mechanical rules surfaced by the
# ranked-preference loop. See _style_check/preferences.yaml for
# the question/ranking each rule was extracted from.
from . import while_true_only                  # Q31
from . import pointer_asterisk_attached        # Q21
from . import switch_case_indent               # Q12
from . import blank_line_after_decl_block      # Q18
from . import blank_line_after_non_final_return  # Q18
from . import non_const_locals_top_of_block    # Q15
from . import function_def_no_style2           # Q4
from . import no_packed_case_labels            # Q42
from . import break_before_operator_forbidden  # Q25
from . import include_order                    # Q13
from . import cast_no_asymmetric_form          # Q22
from . import per_case_braces_when_locals      # Q12
from . import empty_body_braces                # Q44
from . import endif_annotation_long_blocks     # Q38
from . import no_packed_enum_values            # Q23

from . import multiline_call_style

# `non_const_locals_top_of_block` was prototyped but NOT registered:
# CLAUDE.md claims this is a hard rule ("declare at top of enclosing
# block"), but the corpus regularly uses C99 mixed-declaration style:
# kernel/signal.c:21-33 (`int slot = ...` after ASSERT and `signum--`),
# kernel/poll.c:21 (`fs_handle h = ...` mid-loop-body). Other
# canonical files (kernel/fork.c:62-67) do use C89 top-of-function
# style. Both forms are accepted in practice -- the rule isn't a hard
# binary check. Defer until we can distinguish "intentional mid-block
# C99 style" from "drift".

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
   multiline_call_style.RULE,
   while_true_only.RULE,
   pointer_asterisk_attached.RULE,
   switch_case_indent.RULE,
   blank_line_after_decl_block.RULE,
   blank_line_after_non_final_return.RULE,
   non_const_locals_top_of_block.RULE,
   function_def_no_style2.RULE,
   no_packed_case_labels.RULE,
   break_before_operator_forbidden.RULE,
   include_order.RULE,
   cast_no_asymmetric_form.RULE,
   per_case_braces_when_locals.RULE,
   empty_body_braces.RULE,
   endif_annotation_long_blocks.RULE,
   no_packed_enum_values.RULE,
]

RULES_BY_ID = { r.id: r for r in ALL_RULES }
