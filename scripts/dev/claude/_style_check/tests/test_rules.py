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
REPO_ROOT = _parser_mod.find_repo_root() or HERE.parents[4]


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


class TestRulesOnGoldenFiles(unittest.TestCase):
   """Canonical files in the kernel must report zero diagnostics. Files
   with known legacy drift (kernel/exit.c:244 over 80 cols,
   kernel/elf.c:446 over 80 cols) are excluded from this list -- they
   would be real-world cleanup tasks for the user, not bugs in the tool."""

   GOLDEN = [
      'kernel/poll.c',
      'kernel/execve.c',
      'kernel/sched.c',
      'kernel/fork.c',
      'kernel/signal.c',
      'kernel/kmutex.c',
      'include/tilck/kernel/sync.h',
      'include/tilck/common/atomics.h',
      'include/tilck/common/basic_defs.h',
      'include/tilck/kernel/sched.h',
      'include/tilck/kernel/list.h',
      'include/tilck/common/assert.h',
      'userapps/tracer/screen_tracing.c',
      'userapps/dp/dp_main.c',
      'userapps/devshell/devshell.c',
   ]

   def setUp(self):

      build_dir = _parser_mod.resolve_build_dir('build/compile_db')
      self.parser = _parser_mod.Parser(build_dir)

   def test_all_rules_clean_on_golden(self):

      for rel in self.GOLDEN:

         p = REPO_ROOT / rel
         self.assertTrue(p.exists(), "missing golden: {}".format(p))

         applicable = [
            r for r in RULES_BY_ID.values() if r.applies_to_file(p)
         ]

         for r in applicable:

            diags = _run_rule(r, p, self.parser)
            self.assertEqual(
               diags, [],
               "{} fired on golden {}: {}".format(r.id, rel, diags)
            )


if __name__ == '__main__':
   unittest.main()
