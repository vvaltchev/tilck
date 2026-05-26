# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=consider-using-f-string

import unittest

from pathlib import Path

from .. import parser as _parser_mod
from .. import extents
from .. import tokens as _tokens_mod
from ..rules import RULES_BY_ID
from ..rules.base import CheckContext

HERE = Path(__file__).resolve().parent
FIXTURES = HERE / 'fixtures'


def _ctx_for(file_path, parser_obj, need_tu=False, need_comments=False):

   source_bytes = file_path.read_bytes()
   source_text = source_bytes.decode('utf-8', errors='replace')

   ctx = CheckContext(
      file_path=file_path,
      source_bytes=source_bytes,
      source_text=source_text,
      lines=source_text.split('\n'),
      is_header=file_path.suffix in ('.h', '.hpp'),
      is_cpp=file_path.suffix in ('.cpp', '.hpp'),
   )

   if need_tu and parser_obj is not None:

      tu = parser_obj.parse(file_path)

      if tu is not None:
         ctx.tu = tu
         ctx.extents = extents.walk_main_file(tu, file_path)

   if need_comments:
      ctx.comments = _tokens_mod.scan_comments(source_text)

   return ctx


def _run_rule(rule, file_path, parser_obj):

   ctx = _ctx_for(
      file_path,
      parser_obj,
      need_tu=rule.needs_tu,
      need_comments=rule.needs_comments
   )
   return rule.check(ctx)


class TestRulesOnFixtures(unittest.TestCase):

   def setUp(self):

      build_dir = _parser_mod.resolve_build_dir('build/compile_db')
      self.parser = _parser_mod.Parser(build_dir)

   def test_bad_m1_cols_80(self):

      r = RULES_BY_ID['cols_80']
      diags = _run_rule(r, FIXTURES / 'bad_m1.c', self.parser)
      self.assertGreaterEqual(len(diags), 1)
      self.assertTrue(any(d.rule == 'cols_80' for d in diags))

   def test_bad_m1_sizeof_parens(self):

      r = RULES_BY_ID['sizeof_parens']
      diags = _run_rule(r, FIXTURES / 'bad_m1.c', self.parser)
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'sizeof_parens')
      self.assertEqual(diags[0].line, 20)

   def test_bad_m1_static_fn_def(self):

      r = RULES_BY_ID['static_fn_def_type_own_line']
      diags = _run_rule(r, FIXTURES / 'bad_m1.c', self.parser)
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'static_fn_def_type_own_line')

   def test_bad_m1_comment_block_format(self):

      r = RULES_BY_ID['comment_block_multiline_format']
      diags = _run_rule(r, FIXTURES / 'bad_m1.c', self.parser)
      # 2 interior lines without ' * '
      self.assertEqual(len(diags), 2)
      self.assertTrue(all(
         d.rule == 'comment_block_multiline_format' for d in diags
      ))

   def test_bad_pragma_once(self):

      r = RULES_BY_ID['pragma_once']
      diags = _run_rule(r, FIXTURES / 'bad_pragma_once.h', self.parser)
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'pragma_once')

   def test_bad_indent_3sp(self):

      r = RULES_BY_ID['indent_3sp']
      diags = _run_rule(r, FIXTURES / 'bad_indent_3sp.c', self.parser)
      self.assertEqual(len(diags), 2)  # two tab-indented lines

   def test_bad_trailing_ws(self):

      r = RULES_BY_ID['trailing_ws']
      diags = _run_rule(r, FIXTURES / 'bad_trailing_ws.c', self.parser)
      self.assertEqual(len(diags), 2)

   def test_bad_spdx_header(self):

      r = RULES_BY_ID['spdx_header']
      # spdx_header only fires on Tilck-authored paths. The fixture is
      # at scripts/dev/claude/... which doesn't match TILCK_FRAGMENTS,
      # so we explicitly skip the path check by feeding a kernel-path
      # alias via the file's content alone -- in production the rule
      # would fire on a real kernel/ file. Here we just verify the
      # rule's logic against a path-fragment-bypass: not ideal, so we
      # confirm the rule returns 0 on the fixture (path doesn't match
      # TILCK_FRAGMENTS) and rely on the bad_m1.c not having an issue
      # to confirm the negative case.
      diags = _run_rule(r, FIXTURES / 'bad_spdx_header.c', self.parser)
      self.assertEqual(diags, [])

   def test_bad_hex_literal_lowercase(self):

      r = RULES_BY_ID['hex_literal_lowercase']
      diags = _run_rule(
         r, FIXTURES / 'bad_hex_literal_lowercase.c', self.parser
      )
      # 0xABCD, 0X1234, 0xDEADBEEF -> 3 violations
      self.assertEqual(len(diags), 3)
      # Soft rule: must report `warning` severity, not `error`.
      self.assertTrue(
         all(d.severity == 'warning' for d in diags),
         'hex_literal_lowercase must be soft (warning severity)'
      )

   def test_bad_no_void_cast_discard(self):

      r = RULES_BY_ID['no_void_cast_discard']
      diags = _run_rule(
         r, FIXTURES / 'bad_no_void_cast_discard.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertTrue(diags[0].is_gradient)

   def test_bad_void_arglist(self):

      r = RULES_BY_ID['void_arglist']
      diags = _run_rule(r, FIXTURES / 'bad_void_arglist.c', self.parser)
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'void_arglist')

   def test_bad_enum_trailing_comma(self):

      r = RULES_BY_ID['enum_trailing_comma']
      diags = _run_rule(
         r, FIXTURES / 'bad_enum_trailing_comma.c', self.parser
      )
      self.assertEqual(len(diags), 2)

   def test_bad_one_stmt_per_line(self):

      r = RULES_BY_ID['one_stmt_per_line']
      diags = _run_rule(
         r, FIXTURES / 'bad_one_stmt_per_line.c', self.parser
      )
      self.assertEqual(len(diags), 1)

   def test_bad_else_same_line_as_brace(self):

      r = RULES_BY_ID['else_same_line_as_brace']
      diags = _run_rule(
         r, FIXTURES / 'bad_else_same_line_as_brace.c', self.parser
      )
      self.assertEqual(len(diags), 2)

   def test_bad_fn_body_brace_own_line(self):

      r = RULES_BY_ID['fn_body_brace_own_line']
      diags = _run_rule(
         r, FIXTURES / 'bad_fn_body_brace_own_line.c', self.parser
      )
      self.assertEqual(len(diags), 1)

   def test_bad_multiline_call_style(self):

      r = RULES_BY_ID['multiline_call_style']
      diags = _run_rule(
         r, FIXTURES / 'bad_multiline_call_style.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'multiline_call_style')

   # ----- v2 rules -----

   def test_bad_while_true_only(self):

      r = RULES_BY_ID['while_true_only']
      diags = _run_rule(
         r, FIXTURES / 'bad_while_true_only.c', self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(all(d.rule == 'while_true_only' for d in diags))

   def test_bad_pointer_asterisk_attached(self):

      r = RULES_BY_ID['pointer_asterisk_attached']
      diags = _run_rule(
         r, FIXTURES / 'bad_pointer_asterisk_attached.c', self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(
         all(d.rule == 'pointer_asterisk_attached' for d in diags)
      )

   def test_bad_switch_case_indent(self):

      r = RULES_BY_ID['switch_case_indent']
      diags = _run_rule(
         r, FIXTURES / 'bad_switch_case_indent.c', self.parser
      )
      self.assertEqual(len(diags), 3)
      self.assertTrue(all(d.rule == 'switch_case_indent' for d in diags))

   def test_bad_blank_line_after_decl_block(self):

      r = RULES_BY_ID['blank_line_after_decl_block']
      diags = _run_rule(
         r, FIXTURES / 'bad_blank_line_after_decl_block.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'blank_line_after_decl_block')

   def test_bad_blank_line_after_non_final_return(self):

      r = RULES_BY_ID['blank_line_after_non_final_return']
      diags = _run_rule(
         r,
         FIXTURES / 'bad_blank_line_after_non_final_return.c',
         self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(
         all(d.rule == 'blank_line_after_non_final_return' for d in diags)
      )

   def test_bad_non_const_locals_top_of_block(self):

      r = RULES_BY_ID['non_const_locals_top_of_block']
      diags = _run_rule(
         r, FIXTURES / 'bad_non_const_locals_top_of_block.c', self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(
         all(d.rule == 'non_const_locals_top_of_block' for d in diags)
      )

   def test_bad_function_def_no_style2(self):

      r = RULES_BY_ID['function_def_no_style2']
      diags = _run_rule(
         r, FIXTURES / 'bad_function_def_no_style2.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'function_def_no_style2')

   def test_bad_no_packed_case_labels(self):

      r = RULES_BY_ID['no_packed_case_labels']
      diags = _run_rule(
         r, FIXTURES / 'bad_no_packed_case_labels.c', self.parser
      )
      # `case 1: case 2:` = 1 packed pair; `case 3: case 4: case 5:` =
      # 2 additional. Total: 3.
      self.assertEqual(len(diags), 3)
      self.assertTrue(
         all(d.rule == 'no_packed_case_labels' for d in diags)
      )

   def test_bad_break_before_operator_forbidden(self):

      r = RULES_BY_ID['break_before_operator_forbidden']
      diags = _run_rule(
         r,
         FIXTURES / 'bad_break_before_operator_forbidden.c',
         self.parser
      )
      self.assertEqual(len(diags), 3)
      self.assertTrue(
         all(d.rule == 'break_before_operator_forbidden' for d in diags)
      )

   def test_bad_include_order(self):

      r = RULES_BY_ID['include_order']
      diags = _run_rule(
         r, FIXTURES / 'bad_include_order.c', self.parser
      )
      # tilck_gen_headers/ not first AND kernel/ subtree interleaved.
      self.assertEqual(len(diags), 2)
      self.assertTrue(all(d.rule == 'include_order' for d in diags))

   def test_bad_cast_no_asymmetric_form(self):

      r = RULES_BY_ID['cast_no_asymmetric_form']
      diags = _run_rule(
         r, FIXTURES / 'bad_cast_no_asymmetric_form.c', self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(
         all(d.rule == 'cast_no_asymmetric_form' for d in diags)
      )

   def test_bad_per_case_braces_when_locals(self):

      r = RULES_BY_ID['per_case_braces_when_locals']
      diags = _run_rule(
         r, FIXTURES / 'bad_per_case_braces_when_locals.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'per_case_braces_when_locals')

   def test_bad_multiline_call_style_broad(self):

      r = RULES_BY_ID['multiline_call_style']
      diags = _run_rule(
         r,
         FIXTURES / 'bad_multiline_call_style_broad.c',
         self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'multiline_call_style')

   def test_bad_empty_body_braces(self):

      r = RULES_BY_ID['empty_body_braces']
      diags = _run_rule(
         r, FIXTURES / 'bad_empty_body_braces.c', self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(all(d.rule == 'empty_body_braces' for d in diags))

   def test_bad_no_packed_enum_values(self):

      r = RULES_BY_ID['no_packed_enum_values']
      diags = _run_rule(
         r, FIXTURES / 'bad_no_packed_enum_values.c', self.parser
      )
      # 5 enumerators packed on one line -> 4 violations (one per
      # enumerator following the first).
      self.assertEqual(len(diags), 4)
      self.assertTrue(
         all(d.rule == 'no_packed_enum_values' for d in diags)
      )

   def test_bad_harmony_with_neighbors(self):

      r = RULES_BY_ID['harmony_with_neighbors']
      diags = _run_rule(
         r, FIXTURES / 'bad_harmony_with_neighbors.c', self.parser
      )
      # One outlier-long line among short neighbors.
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'harmony_with_neighbors')
      self.assertEqual(diags[0].severity, 'warning')

   def test_bad_comment_tag_choice(self):

      r = RULES_BY_ID['comment_tag_choice']
      diags = _run_rule(
         r, FIXTURES / 'bad_comment_tag_choice.c', self.parser
      )
      self.assertEqual(len(diags), 3)
      self.assertTrue(all(d.severity == 'warning' for d in diags))

   def test_bad_paren_explicit_precedence(self):

      r = RULES_BY_ID['paren_explicit_precedence']
      diags = _run_rule(
         r,
         FIXTURES / 'bad_paren_explicit_precedence.c',
         self.parser
      )
      self.assertGreaterEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'paren_explicit_precedence')
      self.assertEqual(diags[0].severity, 'warning')

   def test_bad_macro_brace_only_forbidden(self):

      r = RULES_BY_ID['macro_brace_only_forbidden']
      diags = _run_rule(
         r,
         FIXTURES / 'bad_macro_brace_only_forbidden.c',
         self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'macro_brace_only_forbidden')
      self.assertEqual(diags[0].severity, 'warning')

   def test_bad_typed_literal_suffix(self):

      r = RULES_BY_ID['typed_literal_suffix']
      diags = _run_rule(
         r, FIXTURES / 'bad_typed_literal_suffix.c', self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(
         all(d.rule == 'typed_literal_suffix' for d in diags)
      )

   def test_bad_assert_split_heterogeneous(self):

      r = RULES_BY_ID['assert_split_heterogeneous']
      diags = _run_rule(
         r,
         FIXTURES / 'bad_assert_split_heterogeneous.c',
         self.parser
      )
      # 4-clause ASSERT fires; single-clause and 2-clause don't.
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'assert_split_heterogeneous')
      self.assertEqual(diags[0].severity, 'warning')

   def test_bad_operator_past_close_paren(self):

      r = RULES_BY_ID['operator_past_close_paren']
      diags = _run_rule(
         r,
         FIXTURES / 'bad_operator_past_close_paren.c',
         self.parser
      )
      # 2 operators (one per wrapped line) should fire.
      self.assertGreaterEqual(len(diags), 1)
      self.assertTrue(
         all(d.rule == 'operator_past_close_paren' for d in diags)
      )

   def test_bad_free_no_null_guard(self):

      r = RULES_BY_ID['free_no_null_guard']
      diags = _run_rule(
         r, FIXTURES / 'bad_free_no_null_guard.c', self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(
         all(d.rule == 'free_no_null_guard' for d in diags)
      )

   def test_bad_align_multiline_operators(self):

      r = RULES_BY_ID['align_multiline_operators']
      diags = _run_rule(
         r,
         FIXTURES / 'bad_align_multiline_operators.c',
         self.parser
      )
      # First && at col 19, second at col 21 -- first one flagged
      # (target column is the rightmost).
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'align_multiline_operators')
      self.assertEqual(diags[0].severity, 'warning')

   def test_bad_blank_line_before_return(self):

      r = RULES_BY_ID['blank_line_before_return']
      diags = _run_rule(
         r, FIXTURES / 'bad_blank_line_before_return.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'blank_line_before_return')

   def test_bad_verbose_type_name(self):

      r = RULES_BY_ID['verbose_type_name']
      diags = _run_rule(
         r, FIXTURES / 'bad_verbose_type_name.c', self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(
         all(d.rule == 'verbose_type_name' for d in diags)
      )

   def test_bad_designator_init_spacing(self):

      r = RULES_BY_ID['designator_init_spacing']
      diags = _run_rule(
         r, FIXTURES / 'bad_designator_init_spacing.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'designator_init_spacing')

   def test_bad_sizeof_parens(self):

      r = RULES_BY_ID['sizeof_parens']
      diags = _run_rule(
         r, FIXTURES / 'bad_sizeof_parens.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'sizeof_parens')

   def test_bad_cast_density(self):

      r = RULES_BY_ID['cast_density']
      diags = _run_rule(
         r, FIXTURES / 'bad_cast_density.c', self.parser
      )
      self.assertEqual(len(diags), 4)
      self.assertTrue(all(d.is_gradient for d in diags))
      self.assertTrue(all(d.rule == 'cast_density' for d in diags))

   def test_bad_ifdef_density(self):

      r = RULES_BY_ID['ifdef_density']
      diags = _run_rule(
         r, FIXTURES / 'bad_ifdef_density.c', self.parser
      )
      self.assertEqual(len(diags), 3)
      self.assertTrue(all(d.is_gradient for d in diags))
      self.assertTrue(all(d.rule == 'ifdef_density' for d in diags))

   def test_bad_else_after_return(self):

      r = RULES_BY_ID['else_after_return']
      diags = _run_rule(
         r, FIXTURES / 'bad_else_after_return.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertTrue(diags[0].is_gradient)
      self.assertEqual(diags[0].rule, 'else_after_return')

   def test_bad_call_cluster_column_align(self):

      r = RULES_BY_ID['call_cluster_column_align']
      diags = _run_rule(
         r, FIXTURES / 'bad_call_cluster_column_align.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertTrue(diags[0].is_gradient)
      self.assertEqual(diags[0].rule, 'call_cluster_column_align')

   def test_bad_static_fn_def_type_own_line(self):

      r = RULES_BY_ID['static_fn_def_type_own_line']
      diags = _run_rule(
         r, FIXTURES / 'bad_static_fn_def_type_own_line.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'static_fn_def_type_own_line')

   def test_bad_define_backslash_align(self):

      r = RULES_BY_ID['define_backslash_align']
      diags = _run_rule(
         r, FIXTURES / 'bad_define_backslash_align.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'define_backslash_align')

   def test_bad_cols_80(self):

      r = RULES_BY_ID['cols_80']
      diags = _run_rule(
         r, FIXTURES / 'bad_cols_80.c', self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'cols_80')

   def test_bad_endif_annotation_long_blocks(self):

      r = RULES_BY_ID['endif_annotation_long_blocks']
      diags = _run_rule(
         r,
         FIXTURES / 'bad_endif_annotation_long_blocks.c',
         self.parser
      )
      # Short block (~3 lines) is compliant without annotation;
      # only the long block's bare #endif fires.
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'endif_annotation_long_blocks')

   def test_bad_multiline_call_vs_long_neighbor(self):

      r = RULES_BY_ID['multiline_call_vs_long_neighbor']
      diags = _run_rule(
         r,
         FIXTURES / 'bad_multiline_call_vs_long_neighbor.c',
         self.parser
      )
      self.assertEqual(len(diags), 1)
      self.assertTrue(diags[0].is_gradient)
      self.assertEqual(
         diags[0].rule, 'multiline_call_vs_long_neighbor'
      )

   # ----- C++ rules -----

   def test_bad_prefer_cpp_cast(self):

      r = RULES_BY_ID['prefer_cpp_cast']
      diags = _run_rule(
         r, FIXTURES / 'bad_prefer_cpp_cast.cpp', self.parser
      )
      self.assertGreaterEqual(len(diags), 2)
      self.assertTrue(all(d.rule == 'prefer_cpp_cast' for d in diags))
      self.assertTrue(all(d.is_gradient for d in diags))

   def test_bad_prefer_nullptr(self):

      r = RULES_BY_ID['prefer_nullptr']
      diags = _run_rule(
         r, FIXTURES / 'bad_prefer_nullptr.cpp', self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(all(d.rule == 'prefer_nullptr' for d in diags))
      self.assertTrue(all(d.is_gradient for d in diags))

   def test_bad_using_namespace_in_header(self):

      r = RULES_BY_ID['using_namespace_in_header']
      diags = _run_rule(
         r,
         FIXTURES / 'bad_using_namespace_in_header.hpp',
         self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(
         all(d.rule == 'using_namespace_in_header' for d in diags)
      )

   def test_bad_void_arglist_cpp(self):

      r = RULES_BY_ID['void_arglist_cpp']
      diags = _run_rule(
         r, FIXTURES / 'bad_void_arglist_cpp.cpp', self.parser
      )
      self.assertEqual(len(diags), 2)
      self.assertTrue(
         all(d.rule == 'void_arglist_cpp' for d in diags)
      )
      self.assertTrue(all(len(d.fixes) > 0 for d in diags))


class TestRulesNoFalsePositives(unittest.TestCase):
   """Synthetic `good_*` fixtures exercise the no-violation code paths
   (rule walks the construct, finds it compliant, returns []).
   These are tool-correctness tests, NOT corpus-quality tests --
   the fixtures live entirely inside the test directory."""

   def setUp(self):

      build_dir = _parser_mod.resolve_build_dir('build/compile_db')
      self.parser = _parser_mod.Parser(build_dir)

   def _assert_no_diags(self, rule_id, fixture_name):

      r = RULES_BY_ID[rule_id]
      diags = _run_rule(r, FIXTURES / fixture_name, self.parser)
      self.assertEqual(
         diags, [],
         '{} fired on compliant fixture {}: {}'.format(
            rule_id, fixture_name, diags
         )
      )

   def test_good_multiline_call_style(self):
      self._assert_no_diags(
         'multiline_call_style', 'good_multiline_call_style.c'
      )

   def test_good_multiline_call_complex(self):
      self._assert_no_diags(
         'multiline_call_style', 'good_multiline_call_complex.c'
      )

   def test_good_static_fn_def(self):
      self._assert_no_diags(
         'static_fn_def_type_own_line', 'good_static_fn_def.c'
      )

   def test_good_pointer_asterisk(self):
      self._assert_no_diags(
         'pointer_asterisk_attached', 'good_pointer_asterisk.c'
      )

   def test_good_empty_body_braces(self):
      self._assert_no_diags(
         'empty_body_braces', 'good_empty_body_braces.c'
      )

   def test_good_no_packed_enum(self):
      self._assert_no_diags(
         'no_packed_enum_values', 'good_no_packed_enum.c'
      )

   def test_good_include_order(self):
      self._assert_no_diags(
         'include_order', 'good_include_order.c'
      )

   def test_good_no_packed_case(self):
      self._assert_no_diags(
         'no_packed_case_labels', 'good_no_packed_case.c'
      )

   def test_good_per_case_braces_when_locals(self):
      # Covers fall-through cases, control-flow stmt bodies with
      # decls in their braced inner scope, and macro expansions
      # producing GCC statement-expression compounds with decls.
      self._assert_no_diags(
         'per_case_braces_when_locals',
         'good_per_case_braces_when_locals.c'
      )

   def test_good_switch_case(self):
      self._assert_no_diags(
         'switch_case_indent', 'good_switch_case.c'
      )

   def test_good_function_def_no_style2(self):
      self._assert_no_diags(
         'function_def_no_style2', 'good_function_def_no_style2.c'
      )

   def test_good_non_const_locals(self):
      self._assert_no_diags(
         'non_const_locals_top_of_block', 'good_non_const_locals.c'
      )

   def test_good_align_multiline_operators(self):

      self._assert_no_diags(
         'align_multiline_operators',
         'good_align_multiline_operators.c'
      )

   def test_good_harmony_with_neighbors(self):

      self._assert_no_diags(
         'harmony_with_neighbors', 'good_harmony_with_neighbors.c'
      )

   def test_good_blank_line_after_decl_block(self):
      # Covers three patterns that previously triggered false
      # positives on real Tilck code:
      #   1) Tight if-body with `decl + use` (kernel/poll.c:191-194
      #      pattern).
      #   2) GCC statement-expression macro expansion -- inner
      #      compound stmt has decls pinned to the macro call site.
      #   3) Sub-block (`{ ... }`) used to narrow temporary scope.
      self._assert_no_diags(
         'blank_line_after_decl_block',
         'good_blank_line_after_decl_block.c'
      )

   def test_good_blank_line_before_return(self):
      self._assert_no_diags(
         'blank_line_before_return',
         'good_blank_line_before_return.c'
      )

   def test_good_verbose_type_name(self):
      self._assert_no_diags(
         'verbose_type_name', 'good_verbose_type_name.c'
      )

   def test_good_designator_init_spacing(self):
      self._assert_no_diags(
         'designator_init_spacing',
         'good_designator_init_spacing.c'
      )

   def test_good_else_same_line_as_brace(self):
      self._assert_no_diags(
         'else_same_line_as_brace',
         'good_else_same_line_as_brace.c'
      )

   def test_good_fn_body_brace_own_line(self):
      self._assert_no_diags(
         'fn_body_brace_own_line',
         'good_fn_body_brace_own_line.c'
      )

   def test_good_sizeof_parens(self):
      self._assert_no_diags(
         'sizeof_parens', 'good_sizeof_parens.c'
      )

   def test_good_free_no_null_guard(self):
      self._assert_no_diags(
         'free_no_null_guard', 'good_free_no_null_guard.c'
      )

   def test_good_trailing_ws(self):
      self._assert_no_diags(
         'trailing_ws', 'good_trailing_ws.c'
      )

   def test_good_void_arglist(self):
      self._assert_no_diags(
         'void_arglist', 'good_void_arglist.c'
      )

   def test_good_cols_80(self):
      self._assert_no_diags(
         'cols_80', 'good_cols_80.c'
      )

   def test_good_cast_density(self):
      self._assert_no_diags(
         'cast_density', 'good_cast_density.c'
      )

   def test_good_ifdef_density(self):
      self._assert_no_diags(
         'ifdef_density', 'good_ifdef_density.c'
      )

   def test_good_else_after_return(self):
      self._assert_no_diags(
         'else_after_return', 'good_else_after_return.c'
      )

   def test_good_call_cluster_column_align(self):
      self._assert_no_diags(
         'call_cluster_column_align',
         'good_call_cluster_column_align.c'
      )

   def test_good_define_backslash_align(self):
      self._assert_no_diags(
         'define_backslash_align',
         'good_define_backslash_align.c'
      )

   def test_good_indent_3sp(self):
      self._assert_no_diags(
         'indent_3sp', 'good_indent_3sp.c'
      )

   # ----- C++ rules -----

   def test_good_prefer_cpp_cast(self):
      self._assert_no_diags(
         'prefer_cpp_cast', 'good_prefer_cpp_cast.cpp'
      )

   def test_good_prefer_nullptr(self):
      self._assert_no_diags(
         'prefer_nullptr', 'good_prefer_nullptr.cpp'
      )

   def test_good_using_namespace_in_header(self):
      self._assert_no_diags(
         'using_namespace_in_header',
         'good_using_namespace_in_header.hpp'
      )

   def test_good_void_arglist_cpp(self):
      self._assert_no_diags(
         'void_arglist_cpp', 'good_void_arglist_cpp.cpp'
      )


class TestPathBasedRules(unittest.TestCase):
   """Tests for rules whose behavior depends on `ctx.file_path`. The
   path is consulted as a string; we synthesize CheckContext directly
   to exercise both the exempt and active branches without needing
   real files at every relevant path."""

   @staticmethod
   def _ctx(path_str, content):

      source_bytes = content.encode('utf-8')

      return CheckContext(
         file_path=Path(path_str),
         source_bytes=source_bytes,
         source_text=content,
         lines=content.split('\n'),
         is_header=path_str.endswith('.h'),
         is_cpp=False,
      )

   def test_spdx_header_fires_on_tilck_path_missing_header(self):

      r = RULES_BY_ID['spdx_header']
      ctx = self._ctx(
         '/repo/kernel/example.c',
         '/* something else on line 1 */\n\nint main(void) { return 0; }\n'
      )
      diags = r.check(ctx)
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'spdx_header')
      self.assertEqual(diags[0].line, 1)

   def test_spdx_header_passes_on_tilck_path_with_header(self):

      r = RULES_BY_ID['spdx_header']
      ctx = self._ctx(
         '/repo/kernel/example.c',
         '/* SPDX-License-Identifier: BSD-2-Clause */\n\nint x;\n'
      )
      self.assertEqual(r.check(ctx), [])

   def test_spdx_header_skips_exempt_path(self):

      r = RULES_BY_ID['spdx_header']
      ctx = self._ctx(
         '/repo/3rd_party/foo/bar.c',
         'whatever\n'
      )
      self.assertEqual(r.check(ctx), [])

   def test_spdx_header_skips_non_tilck_path(self):

      r = RULES_BY_ID['spdx_header']
      ctx = self._ctx(
         '/some/other/place/file.c',
         'whatever\n'
      )
      self.assertEqual(r.check(ctx), [])

   def test_spdx_header_skips_empty_file(self):

      r = RULES_BY_ID['spdx_header']
      ctx = self._ctx('/repo/kernel/empty.c', '')
      ctx.lines = []   # empty file: split('') -> [''], force to []
      self.assertEqual(r.check(ctx), [])

   def test_include_order_skips_exempt_path(self):

      r = RULES_BY_ID['include_order']
      ctx = self._ctx(
         '/repo/3rd_party/foo/file.c',
         '#include <tilck/kernel/sync.h>\n'
         '#include <tilck_gen_headers/config.h>\n'
      )
      self.assertEqual(r.check(ctx), [])

   def test_include_order_skips_header_file(self):

      r = RULES_BY_ID['include_order']
      ctx = self._ctx(
         '/repo/include/tilck/kernel/foo.h',
         '#include <tilck/kernel/sync.h>\n'
         '#include <tilck_gen_headers/config.h>\n'
      )
      # applies_to_file filter rejects .h files for this rule.
      self.assertFalse(r.applies_to_file(Path('/repo/include/foo.h')))

   def test_include_order_no_includes_returns_empty(self):

      r = RULES_BY_ID['include_order']
      ctx = self._ctx(
         '/repo/kernel/no_includes.c',
         'int main(void) { return 0; }\n'
      )
      self.assertEqual(r.check(ctx), [])

   def test_include_order_only_local_includes_returns_empty(self):

      r = RULES_BY_ID['include_order']
      ctx = self._ctx(
         '/repo/kernel/local_only.c',
         '#include "foo.h"\n#include "bar.h"\n'
      )
      # Local quoted includes are not subject to grouping; should
      # produce zero diagnostics.
      self.assertEqual(r.check(ctx), [])


class TestRulesGracefulWithoutTU(unittest.TestCase):
   """Every rule with `needs_tu = True` must early-return [] when
   `ctx.tu` is None (e.g. compile_db missed the file). Catches
   accidental NPE on a degraded code path."""

   @staticmethod
   def _bare_ctx():

      content = 'int main(void) { return 0; }\n'

      return CheckContext(
         file_path=Path('/fake/kernel/example.c'),
         source_bytes=content.encode('utf-8'),
         source_text=content,
         lines=content.split('\n'),
         is_header=False,
         is_cpp=False,
      )

   def test_every_needs_tu_rule_handles_none_tu(self):

      ctx = self._bare_ctx()
      tu_rules = [r for r in RULES_BY_ID.values() if r.needs_tu]

      # Confidence check: we should have many of these.
      self.assertGreater(len(tu_rules), 5)

      for r in tu_rules:

         ctx.tu = None
         diags = r.check(ctx)
         self.assertEqual(
            diags, [],
            '{} did not gracefully early-return on tu=None: {}'.format(
               r.id, diags
            )
         )


class TestAggregator(unittest.TestCase):
   """Per-function rollup: function extraction from a TU, diagnostic
   attribution, verdict computation."""

   def setUp(self):

      from .. import aggregator as _agg
      self._agg = _agg

      build_dir = _parser_mod.resolve_build_dir('build/compile_db')
      self.parser = _parser_mod.Parser(build_dir)

   def test_function_region_verdict_clean(self):

      from ..rules.base import Diagnostic

      f = self._agg.FunctionRegion(
         name='foo', start_line=1, end_line=10, stmt_count=10
      )
      self.assertEqual(f.verdict, 'clean')   # no diagnostics
      f.diagnostics.append(Diagnostic(
         file='x', line=2, col=1, end_line=2, end_col=10,
         rule='r', severity='warning', message='m', score=-0.5,
      ))
      # -0.5 / 10 = -0.05 -> >= -0.1 -> clean
      self.assertEqual(f.verdict, 'clean')

   def test_function_region_verdict_drift(self):

      from ..rules.base import Diagnostic

      f = self._agg.FunctionRegion(
         name='foo', start_line=1, end_line=10, stmt_count=10
      )
      f.diagnostics.append(Diagnostic(
         file='x', line=2, col=1, end_line=2, end_col=10,
         rule='r', severity='warning', message='m', score=-3.0,
      ))
      # -3.0 / 10 = -0.30 -> between drift and ugly thresholds -> drift
      self.assertEqual(f.verdict, 'drift')

   def test_function_region_verdict_ugly(self):

      from ..rules.base import Diagnostic

      f = self._agg.FunctionRegion(
         name='foo', start_line=1, end_line=10, stmt_count=10
      )
      f.diagnostics.append(Diagnostic(
         file='x', line=2, col=1, end_line=2, end_col=10,
         rule='r', severity='warning', message='m', score=-6.0,
      ))
      # -6.0 / 10 = -0.6 -> below drift floor -> ugly
      self.assertEqual(f.verdict, 'ugly')

   def test_function_region_verdict_broken(self):

      from ..rules.base import Diagnostic

      f = self._agg.FunctionRegion(
         name='foo', start_line=1, end_line=10, stmt_count=10
      )
      f.diagnostics.append(Diagnostic(
         file='x', line=2, col=1, end_line=2, end_col=10,
         rule='r', severity='error', message='m', score=-10.0,
      ))
      # Any error severity -> broken regardless of normalized score
      self.assertEqual(f.verdict, 'broken')
      self.assertEqual(f.hard_violations, 1)
      self.assertEqual(f.soft_violations, 0)

   def test_extract_functions_no_tu_returns_empty(self):

      result = self._agg.extract_functions(None, Path('/fake.c'))
      self.assertEqual(result, [])

   def test_build_file_summary_no_tu(self):

      from ..rules.base import Diagnostic

      d = Diagnostic(file='x.c', line=1, col=1, end_line=1, end_col=5,
                     rule='r', severity='warning', message='m')
      summary = self._agg.build_file_summary(
         file_path=Path('/fake.c'),
         tu=None,
         lines=['int x = 0;'],
         diagnostics=[d],
      )
      self.assertEqual(summary.functions, [])
      self.assertEqual(len(summary.file_level_diagnostics), 1)


class TestStyleConfig(unittest.TestCase):
   """`.style.yml` parsing and cascading resolution."""

   def setUp(self):

      from .. import config as _config
      self._config = _config

   def test_parse_ignore_true(self):

      d = self._config._parse_simple_yaml('ignore: true\n')
      self.assertEqual(d, {'ignore': True})

   def test_parse_list_and_scalar(self):

      text = (
         '# comment\n'
         'ignore: false\n'
         'disabled:\n'
         '  - cols_80\n'
         '  - pragma_once\n'
         '\n'
         'enabled_only:\n'
         '  - sizeof_parens\n'
      )
      d = self._config._parse_simple_yaml(text)
      self.assertEqual(d['ignore'], False)
      self.assertEqual(d['disabled'], ['cols_80', 'pragma_once'])
      self.assertEqual(d['enabled_only'], ['sizeof_parens'])

   def test_parse_quoted_values(self):

      text = "key: \"hello\"\nlist:\n  - 'foo'\n  - bar\n"
      d = self._config._parse_simple_yaml(text)
      self.assertEqual(d['key'], 'hello')
      self.assertEqual(d['list'], ['foo', 'bar'])

   def test_resolve_walks_up_and_merges(self):

      import tempfile

      with tempfile.TemporaryDirectory() as td:

         root = Path(td)
         (root / 'project').mkdir()
         (root / 'project' / 'sub').mkdir()
         (root / 'project' / 'sub' / 'leaf').mkdir()

         (root / 'project' / '.style.yml').write_text(
            'disabled:\n  - rule_a\n'
         )
         (root / 'project' / 'sub' / '.style.yml').write_text(
            'disabled:\n  - rule_b\n'
            'enabled_only:\n  - rule_c\n  - rule_b\n'
         )

         leaf_file = root / 'project' / 'sub' / 'leaf' / 'x.c'
         leaf_file.write_text('// dummy\n')

         cfg = self._config.resolve_config(leaf_file, root / 'project')
         self.assertEqual(cfg.disabled, {'rule_a', 'rule_b'})
         self.assertEqual(cfg.enabled_only, {'rule_c', 'rule_b'})
         self.assertFalse(cfg.ignore)
         self.assertEqual(len(cfg.sources), 2)

   def test_resolve_ignore_short_circuits(self):

      import tempfile

      with tempfile.TemporaryDirectory() as td:

         root = Path(td)
         (root / 'vendor').mkdir()
         (root / 'vendor' / '.style.yml').write_text('ignore: true\n')

         leaf = root / 'vendor' / 'multiboot.h'
         leaf.write_text('#pragma once\n')

         cfg = self._config.resolve_config(leaf, root)
         self.assertTrue(cfg.ignore)

   def test_apply_to_rules_disabled(self):

      from ..rules import ALL_RULES

      cfg = self._config.StyleConfig(disabled={'cols_80'})
      filtered = self._config.apply_to_rules(ALL_RULES, cfg)
      self.assertNotIn('cols_80', {r.id for r in filtered})
      self.assertGreater(len(filtered), 0)

   def test_apply_to_rules_enabled_only(self):

      from ..rules import ALL_RULES

      cfg = self._config.StyleConfig(
         enabled_only={'cols_80', 'pragma_once'}
      )
      filtered = self._config.apply_to_rules(ALL_RULES, cfg)
      self.assertEqual(
         {r.id for r in filtered}, {'cols_80', 'pragma_once'}
      )


if __name__ == '__main__':
   unittest.main()
