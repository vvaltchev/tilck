# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=protected-access

import os
import unittest

from .. import conditionals as C
from .. import configs as _configs
from .. import parser as _parser_mod
from . import fixture_build

class TestConditionals(unittest.TestCase):

   def test_top_level_only(self):

      regs = C.scan('/* c */\nint x;\n\n#define A 1\n')
      self.assertEqual(len(regs), 1)
      self.assertEqual(regs[0].id, 0)
      self.assertEqual(regs[0].observable, {1, 2, 4})
      self.assertEqual(regs[0].code, {2})

   def test_branches_and_nesting(self):

      text = '\n'.join([
         '#if A',              # 1
         'int a;',             # 2
         '#ifdef B',           # 3
         'int b;',             # 4
         '#endif',             # 5
         '#elif C',            # 6
         '   /* only c */',    # 7
         '#else',              # 8
         '#define D 1',        # 9
         '#endif',             # 10
         'int top;',           # 11
      ])
      r = C.by_id(C.scan(text))

      self.assertEqual(sorted(r), [0, 1, 3, 6, 8])
      self.assertEqual(r[1].observable, {2})        # not 4: nested
      self.assertEqual(r[3].parent, 1)
      self.assertEqual(r[3].code, {4})
      self.assertEqual((r[6].observable, r[6].code), ({7}, set()))
      self.assertEqual((r[8].observable, r[8].code), ({9}, set()))
      self.assertEqual(r[8].conditions, ['#if A', '#elif C', '#else'])
      self.assertEqual(r[0].observable, {11})
      self.assertEqual([x.id for x in C.ancestors(r, 3)], [3, 1, 0])

   def test_continuations_comments_strings(self):

      text = '\n'.join([
         '#if defined(A) || \\',     # 1
         '    defined(B)',           # 2: part of the #if
         'char *s = "/* no";',       # 3: not a comment
         '/*',                       # 4
         '#if NOT_A_DIRECTIVE',      # 5: inside a comment
         '*/',                       # 6
         '#error unobservable',      # 7
         '#endif',                   # 8
      ])
      r = C.by_id(C.scan(text))

      self.assertEqual(sorted(r), [0, 1])
      self.assertEqual(r[1].observable, {3, 4, 5, 6})
      self.assertEqual(r[1].code, {3})


class TestCompilerView(unittest.TestCase):

   def test_evidence_follows_line_markers(self):

      out = '\n'.join([
         '# 0 "/src/a.c"',
         '# 1 "<built-in>"',
         'junk from the built-ins',
         '# 1 "/src/a.c"',
         '/* line 1 */',
         '',
         'int x;',
         '# 1 "/src/h.h" 1',
         'int h;',
         '# 4 "/src/a.c" 2',
         '',
         'int y;',
      ])
      ev = _configs.evidence(out, '/')

      self.assertEqual(ev[os.path.realpath('/src/a.c')], {1, 3, 5})
      self.assertEqual(ev[os.path.realpath('/src/h.h')], {1})

   def test_strip_io(self):

      flags = _configs._strip_io(
         ['-I', 'inc', '-c', 'x.c', '-o', 'x.o', '-MD', '-MF', 'x.d',
          '-MTx.o', '-DX=1', '-ox2.o'],
         '/d', '/d/x.c',
      )
      self.assertEqual(flags, ('-I', 'inc', '-DX=1'))

   def test_std_of(self):

      gnu17 = '#define __STDC_VERSION__ 201710L\n'
      c11 = '#define __STDC_VERSION__ 201112L\n#define __STRICT_ANSI__ 1\n'
      cxx = '#define __cplusplus 201703L\n'

      self.assertEqual(_configs._std_of(gnu17, 'c'), 'gnu17')
      self.assertEqual(_configs._std_of(c11, 'c'), 'c11')
      self.assertEqual(_configs._std_of(cxx, 'c++'), 'gnu++17')


class TestParsingFixtures(unittest.TestCase):
   """The fixtures, parsed through their configuration (see
   fixture_build.py), take the path real sources take."""

   def setUp(self):
      self.parser = fixture_build.parser()

   def test_every_fixture_parses_cleanly(self):

      n = 0

      for f in sorted(fixture_build.FIXTURES.iterdir()):

         if f.suffix not in ('.c', '.cpp'):
            continue

         n += 1
         fp = self.parser.parse_file(str(f))
         self.assertTrue(fp.parses, f.name)
         self.assertEqual(
            [(p.kind, p.message) for p in fp.problems], [], f.name
         )

      self.assertGreater(n, 50)

   def test_not_compiled_is_an_error(self):

      fp = self.parser.parse_file(
         str(fixture_build.FIXTURES / 'bad_pragma_once.h')
      )
      self.assertEqual(fp.parses, [])
      self.assertEqual([p.kind for p in fp.problems], ['not_compiled'])
      self.assertTrue(fp.failed)

   def test_a_divergence_is_an_error(self):
      """Make the compiler's view miss a branch libclang takes: the
      parse must report the divergence and fail."""

      p = fixture_build.parser()
      found = None

      for e, v in p.views():

         if e.file not in v.active:
            continue

         by_id = C.by_id(p.regions(e.file))

         for rid in sorted(v.active[e.file]):
            if rid and not _parser_mod._compiler_specific(by_id, rid):
               found = (e, v, rid)
               break

         if found:
            break

      self.assertIsNotNone(found, 'no fixture has an #if branch')
      e, v, rid = found
      saved = v.active[e.file]
      v.active[e.file] = saved - {rid}

      try:
         fp = p.parse_file(e.file)
      finally:
         v.active[e.file] = saved

      self.assertIn(('divergence', rid),
                    [(x.kind, x.line) for x in fp.problems])
      self.assertTrue(fp.failed)


class TestProjectPredicate(unittest.TestCase):

   def test_build_trees_and_system_paths_are_not_project(self):

      repo = _parser_mod.find_repo_root()
      is_project = _parser_mod.project_predicate(repo, [])

      self.assertTrue(is_project(str(repo / 'kernel' / 'sched.c')))
      self.assertFalse(is_project('/usr/include/stdio.h'))
      self.assertFalse(is_project(str(repo / 'kernel' / 'sched.o')))
