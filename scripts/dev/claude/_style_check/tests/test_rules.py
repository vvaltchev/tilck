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
      is_header=file_path.suffix == '.h',
      is_cpp=False,
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
      self.assertEqual(len(diags), 2)

   def test_bad_void_arglist(self):

      r = RULES_BY_ID['void_arglist']
      diags = _run_rule(r, FIXTURES / 'bad_void_arglist.c', self.parser)
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].rule, 'void_arglist')

   def test_bad_no_trailing_enum_comma(self):

      r = RULES_BY_ID['no_trailing_enum_comma']
      diags = _run_rule(
         r, FIXTURES / 'bad_no_trailing_enum_comma.c', self.parser
      )
      self.assertEqual(len(diags), 1)

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


if __name__ == '__main__':
   unittest.main()
