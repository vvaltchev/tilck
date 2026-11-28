# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring

"""
The Python rules, each on short snippets: the lines it reports in code
that breaks the style, and silence on code that keeps it. The style is
the one docs/plans/python-style.md infers from the author's Python.
"""

import os
import subprocess
import sys
import tempfile
import textwrap
import unittest

from pathlib import Path

from .. import harmony
from .. import pyctx
from ..languages import language_of
from ..rules import RULES_BY_ID
from ..rules.base import CheckContext

HERE = Path(__file__).resolve().parent
FIXTURES = HERE / 'fixtures'
SPDX = '# SPDX-License-Identifier: BSD-2-Clause\n'


def _ctx(text: str, indent: int = 3, name: str = 'snippet.py'):

   ctx = CheckContext(
      file_path=Path(name),
      source_bytes=text.encode('utf-8'),
      source_text=text,
      lines=text.split('\n'),
      is_header=False,
      is_cpp=False,
      indent=indent,
      language='python',
   )

   ctx.py = pyctx.build(text)
   return ctx


def _lines(rule_id: str, text: str, indent: int = 3):
   """The lines `rule_id` reports in `text` (dedented)."""

   ctx = _ctx(textwrap.dedent(text), indent)
   return [d.line for d in RULES_BY_ID[rule_id].check(ctx)]


class TestLanguageOf(unittest.TestCase):

   def test_suffixes(self):

      self.assertEqual(language_of(Path('a.py')), 'python')
      self.assertEqual(language_of(Path('a.c')), 'c')
      self.assertEqual(language_of(Path('a.hpp')), 'c')
      self.assertIsNone(language_of(Path('a.rb')))

   def test_shebangs(self):

      cases = {
         '#!/usr/bin/env python3\n': 'python',
         '#!/usr/bin/python3.11\n': 'python',
         '#!/usr/bin/env python\n': 'python',
         '#!/usr/bin/env bash\n': None,
         'print(1)\n': None,
      }

      with tempfile.TemporaryDirectory() as d:
         for i, (first, lang) in enumerate(cases.items()):
            p = Path(d) / 'script{}'.format(i)
            p.write_text(first)
            self.assertEqual(language_of(p), lang, first)

   def test_rules_pick_their_languages(self):

      py, c = Path('a.py'), Path('a.c')

      self.assertTrue(RULES_BY_ID['cols_80'].applies_to_file(py))
      self.assertTrue(RULES_BY_ID['trailing_ws'].applies_to_file(py))
      self.assertFalse(RULES_BY_ID['sizeof_parens'].applies_to_file(py))
      self.assertFalse(RULES_BY_ID['py_indent'].applies_to_file(c))
      self.assertTrue(RULES_BY_ID['py_indent'].applies_to_file(py))


class TestPyContext(unittest.TestCase):

   def test_syntax_error_is_kept_not_raised(self):

      ctx = _ctx('x = 1\ndef f(:\n   pass\n')

      self.assertTrue(ctx.py.error.startswith('SyntaxError'))
      self.assertEqual(ctx.py.error_line, 2)
      self.assertEqual(RULES_BY_ID['py_indent'].check(ctx), [])

   def test_blocks_and_chains(self):

      py = pyctx.build(textwrap.dedent('''\
         if a:
            x = 1
         elif b:
            x = 2
         else:
            x = 3
         if c: x = 4
      '''))

      keywords = [b.keyword for b in py.blocks]
      self.assertEqual(sorted(keywords), ['elif', 'else', 'if'])
      self.assertEqual(len({b.chain for b in py.blocks}), 1)

   def test_logical_lines(self):

      py = pyctx.build('x = foo(1,\n        2)\ny = 3\n')

      self.assertEqual(py.logical_start[1], 1)
      self.assertEqual(py.logical_start[2], 1)
      self.assertEqual(py.logical_start[3], 3)


class TestSpdxHeader(unittest.TestCase):

   def test_missing(self):

      self.assertEqual(_lines('py_spdx_header', 'import os\n'), [1])
      self.assertEqual(
         _lines('py_spdx_header', '#!/usr/bin/env python3\nimport os\n'), [2]
      )

   def test_present(self):

      self.assertEqual(_lines('py_spdx_header', SPDX + 'import os\n'), [])
      self.assertEqual(
         _lines('py_spdx_header', '#!/usr/bin/env python3\n' + SPDX), []
      )

   def test_empty_file(self):
      self.assertEqual(_lines('py_spdx_header', ''), [])


class TestIndent(unittest.TestCase):

   CODE = '''\
      def f(x):
          if x:
              return 1
          return 2
   '''

   def test_width(self):
      self.assertEqual(_lines('py_indent', self.CODE, indent=3), [2, 3])
      self.assertEqual(_lines('py_indent', self.CODE, indent=4), [])

   def test_tab(self):
      self.assertEqual(_lines('py_indent', 'def f():\n\treturn 1\n'), [2])


class TestHangingBracket(unittest.TestCase):

   def test_good(self):

      self.assertEqual(_lines('py_hanging_bracket', '''\
         def f():
            x = foo(
               a,
               b,
            )
            return {
               'k': [
                  1,
               ],
            }
      '''), [])

   def test_element_column(self):

      self.assertEqual(_lines('py_hanging_bracket', '''\
         x = foo(
               a,
            b,
         )
      '''), [2])

   def test_close(self):

      self.assertEqual(_lines('py_hanging_bracket', '''\
         x = foo(
            a,
            b)
         y = foo(
            a,
            )
      '''), [3, 6])


class TestAlignedBracket(unittest.TestCase):

   def test_good(self):

      self.assertEqual(_lines('py_aligned_bracket', '''\
         x = foo(a,
                 b + c +
                    d,
                 e)
      '''), [])

   def test_misaligned_and_close_alone(self):

      self.assertEqual(_lines('py_aligned_bracket', '''\
         x = foo(a,
                   b,
                 c
         )
      '''), [2, 4])


class TestBlankAfterJump(unittest.TestCase):

   def test_bad(self):

      self.assertEqual(_lines('py_blank_after_jump', '''\
         def f(x):
            if x:
               return 1
            return 2
      '''), [4])

   def test_good(self):

      self.assertEqual(_lines('py_blank_after_jump', '''\
         def f(x):
            for y in x:
               if y:
                  continue

               print(y)

            if x:
               return 1
            else:
               return 2
      '''), [])


class TestBlankAfterHeader(unittest.TestCase):

   def test_single_group_spaced(self):

      self.assertEqual(_lines('py_blank_after_header', '''\
         def f():

            x = 1
            return x
      '''), [2])

   def test_groups_not_spaced(self):

      self.assertEqual(_lines('py_blank_after_header', '''\
         def function_with_a_long_name(argument):
            x = 1

            return x
      '''), [1])

   def test_short_header_does_not_hide_the_body(self):

      self.assertEqual(_lines('py_blank_after_header', '''\
         if x:
            value = compute_something(x)

            print(value)
      '''), [])

   def test_docstring_paragraphs_are_not_groups(self):

      self.assertEqual(_lines('py_blank_after_header', '''\
         def a_function_with_a_long_name():
            """The first paragraph.

            The second one.
            """
            return 1
      '''), [])

   def test_multi_line_header(self):

      self.assertEqual(_lines('py_blank_after_header', '''\
         def f(a,
               b):
            return a
      '''), [2])

   def test_multi_line_header_ending_in_a_bracket(self):

      self.assertEqual(_lines('py_blank_after_header', '''\
         if (a and
             (b or c)):

            x = 1
         if (a and
             (b or c)):
            x = 1
      '''), [6])

   def test_spaced_heavy_bodies(self):

      self.assertEqual(_lines('py_blank_after_header', '''\
         def f(x):

            for i in x:
               print(i)

         def g(x):

            return foo(x,
                       x)
      '''), [])

   def test_spaced_like_a_peer(self):

      self.assertEqual(_lines('py_blank_after_header', '''\
         def f(x):

            if x:

               a = 1

               b = 2

            else:

               c = 3

            return a
      '''), [])


class TestBranchSymmetry(unittest.TestCase):

   def test_spaced_branch(self):

      self.assertEqual(_lines('py_branch_symmetry', '''\
         if x:

            a = 1
         else:
            b = 2
      '''), [4])

   def test_compact_branch(self):

      self.assertEqual(_lines('py_branch_symmetry', '''\
         if x:
            a = 1

         else:
            b = 2
      '''), [4])

   def test_good(self):

      self.assertEqual(_lines('py_branch_symmetry', '''\
         try:

            a = 1

         except OSError:
            b = 2
         finally:
            c = 3
      '''), [])


class TestBlankBetweenDefs(unittest.TestCase):

   def test_counts(self):

      self.assertEqual(_lines('py_blank_between_defs', '''\
         class A:

            def f(self):
               pass
            def g(self):
               pass


            # h's own comment
            def h(self):
               pass

         def top():
            pass


         def main():
            pass
      '''), [5, 10])


class TestNoDoubleBlank(unittest.TestCase):

   def test_inside_a_function(self):

      self.assertEqual(_lines('py_no_double_blank', '''\
         def f():

            a = 1


            return a
      '''), [4])

   def test_inside_a_string(self):

      self.assertEqual(_lines('py_no_double_blank', '''\
         def f():
            text = """first


            second"""
            return text
      '''), [])

   def test_before_a_nested_def(self):

      self.assertEqual(_lines('py_no_double_blank', '''\
         class A:

            x = 1


            def f(self):
               pass
      '''), [])


class TestDefDefaultSpacing(unittest.TestCase):

   def test_defaults(self):

      self.assertEqual(_lines('py_def_default_spacing', '''\
         def f(a, b=1, c = 2, *, d: int=3, e: int = 4):
            return g(a, key=b)
      '''), [1, 1])


class TestDictColonUniform(unittest.TestCase):

   def test_forms(self):

      self.assertEqual(_lines('py_dict_colon_uniform', '''\
         a = {'x': 1, 'y': 2}
         b = {'x' : 1, 'y' : 2}
         c = {'x': 1, 'y' : 2}
         d = {
            'xx':   1,
            'yyyy': 2,
         }
      '''), [3])


class TestBackslashAlign(unittest.TestCase):

   def test_columns(self):

      self.assertEqual(_lines('py_backslash_align', '''\
         x = a +    \\
             b +    \\
             c
         y = a + \\
             b +    \\
             c
      '''), [4])


class TestAssertParens(unittest.TestCase):

   def test_forms(self):

      self.assertEqual(_lines('py_assert_parens', '''\
         assert(x)
         assert x
         assert x, 'message'
      '''), [2])


class TestHarmony(unittest.TestCase):

   BODY = '''\
      def foo(a, b):

         x = a + b
         y = a - b
         z = x * y
         w = z + 1
         v = w + 2
         {}
         u = v + 3
         return u
   '''

   def test_outlier(self):

      long = ('result_of_it_all = compute_something_long(x, y) + '
              'other_thing(z, w)')
      self.assertEqual(_lines('py_harmony', self.BODY.format(long)), [8])

   def test_message_lines_are_excused(self):

      long = 'print("the value of the long computation is: {}".format(v))'
      self.assertEqual(_lines('py_harmony', self.BODY.format(long)), [])

   def test_continuation_lines_are_not_judged(self):

      long = 'q = foo(x,\n            {})'.format('a' * 60)
      self.assertEqual(_lines('py_harmony', self.BODY.format(long)), [])

   def test_shared_model(self):

      # The C rule and the Python rule judge with one model.
      normal = [(n, 12) for n in range(1, 8)] + [(8, 76)]
      lens = [length for _, length in normal]
      found = harmony.outliers(lens, normal, lambda n: False)

      self.assertEqual([o.line for o in found], [8])
      self.assertEqual(harmony.outliers(lens, normal, lambda n: True), [])


class TestCli(unittest.TestCase):
   """`check` on Python files alone: no build is needed, and a file
   Python cannot parse fails the run."""

   def _run(self, *args):

      env = dict(os.environ, PYTHONPATH=str(HERE.parent.parent))
      cmd = [sys.executable, '-m', '_style_check', 'check'] + list(args)

      return subprocess.run(cmd, env=env, capture_output=True, text=True,
                            check=False)

   def test_parse_only(self):

      with tempfile.TemporaryDirectory() as d:

         good = Path(d) / 'good.py'
         bad = Path(d) / 'bad.py'
         good.write_text(SPDX + 'x = 1\n')
         bad.write_text(SPDX + 'def f(:\n   pass\n')

         res = self._run('--parse-only', str(good))
         self.assertEqual(res.returncode, 0, res.stderr)
         self.assertIn('parsed 1 Python file(s): 0 failed', res.stdout)

         res = self._run('--parse-only', str(good), str(bad))
         self.assertEqual(res.returncode, 1, res.stderr)
         self.assertIn('parsed 2 Python file(s): 1 failed', res.stdout)
         self.assertIn('bad.py:2:', res.stdout)


class TestGoodFile(unittest.TestCase):
   """A whole file in the style: no Python rule reports anything."""

   def test_good_python_style(self):

      path = FIXTURES / 'good_python_style.py'
      text = path.read_text()
      ctx = _ctx(text, name=str(path))
      found = []

      for rule in RULES_BY_ID.values():
         if rule.applies_to_file(path):
            found += ['{}:{}'.format(d.rule, d.line) for d in rule.check(ctx)]

      self.assertEqual(found, [])


if __name__ == '__main__':
   unittest.main()
