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
from . import enum_trailing_comma
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

# Context-sensitive soft rules
from . import harmony_with_neighbors           # Q22b
from . import align_multiline_operators        # Q10

# Additional soft rules from the preferences corpus
from . import define_backslash_align
from . import comment_tag_choice               # Q29
from . import paren_explicit_precedence        # Q37
from . import macro_brace_only_forbidden       # Q28
from . import typed_literal_suffix             # Q47
from . import free_no_null_guard               # Q11
from . import assert_split_heterogeneous       # Q43
from . import operator_past_close_paren        # Q25 refinement

from . import multiline_call_style
from . import multiline_call_vs_long_neighbor  # harmony: wrapped-arg vs long neighbor
from . import call_cluster_column_align        # columnar alignment in call tables
from . import else_after_return                # Q49
from . import blank_line_before_return
from . import designator_init_spacing

# Verbose-type-name rule
from . import verbose_type_name

# Gradient-only density rules
from . import ifdef_density
from . import cast_density

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
   enum_trailing_comma.RULE,
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
   harmony_with_neighbors.RULE,
   align_multiline_operators.RULE,
   comment_tag_choice.RULE,
   paren_explicit_precedence.RULE,
   macro_brace_only_forbidden.RULE,
   typed_literal_suffix.RULE,
   free_no_null_guard.RULE,
   assert_split_heterogeneous.RULE,
   operator_past_close_paren.RULE,
   multiline_call_vs_long_neighbor.RULE,
   call_cluster_column_align.RULE,
   define_backslash_align.RULE,
   else_after_return.RULE,
   blank_line_before_return.RULE,
   designator_init_spacing.RULE,
   verbose_type_name.RULE,
   ifdef_density.RULE,
   cast_density.RULE,
]

RULES_BY_ID = { r.id: r for r in ALL_RULES }
