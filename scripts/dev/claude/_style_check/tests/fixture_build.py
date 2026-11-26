# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-function-docstring
"""
The build configuration the tests parse their fixtures with.

The fixtures are kernel-style code (they include <tilck/...> headers)
that no build compiles. The tests compile them, on paper, as the
checkout's build compiles its kernel: a temporary build tree whose
compile DB lists every fixture with the exact command of a kernel C
file of the first build tree (a gtests C++ file, for the .cpp
fixtures). Through it the fixtures take the same path as real
sources: the compiler's view, the parse, the checks against it.
"""

import os
import json
import atexit
import shutil
import tempfile

from pathlib import Path

from .. import configs as _configs
from .. import parser as _parser_mod

HERE = Path(__file__).resolve().parent
FIXTURES = HERE / 'fixtures'

_PARSER = None


class NoBuild(Exception):
   pass


def _donor(cfg, lang, subdir):
   """An entry of `cfg` compiling a `lang` file under `subdir`."""

   repo = _parser_mod.find_repo_root()
   where = str(repo / subdir) + os.sep

   for e in cfg.entries:
      if e.lang == lang and e.file.startswith(where):
         return e

   return None


def parser():
   """The Parser over the fixtures' configuration, made once."""

   global _PARSER  # pylint: disable=global-statement

   if _PARSER is not None:
      return _PARSER

   repo = _parser_mod.find_repo_root()
   trees = _configs.discover(repo) if repo else []

   if not trees:
      raise NoBuild(
         'the tests parse their fixtures with the commands of a Tilck '
         'build: build one first (./scripts/cmake_run && make)'
      )

   base = _configs.resolve_config(str(trees[0]), repo)
   donors = {
      'c': _donor(base, 'c', 'kernel'),
      'c++': _donor(base, 'c++', 'tests/unit'),
   }

   tmp = Path(tempfile.mkdtemp(prefix='style_check_fixtures_'))
   atexit.register(shutil.rmtree, tmp, True)
   db = []

   for f in sorted(FIXTURES.iterdir()):

      lang = _configs.LANG_BY_SUFFIX.get(f.suffix)
      d = donors.get(lang)

      if d is None:
         continue

      db.append({
         'directory': d.directory,
         'file': str(f),
         'arguments': [d.compiler, *d.flags, '-c', str(f),
                       '-o', os.devnull],
      })

   (tmp / 'compile_db').mkdir()

   with open(tmp / 'compile_db' / 'compile_commands.json', 'w',
             encoding='utf-8') as fh:
      json.dump(db, fh)

   cfg = _configs.Config('fixtures', tmp, tmp / 'compile_db')
   prefix = str(FIXTURES) + os.sep
   _PARSER = _parser_mod.Parser([cfg], lambda p: p.startswith(prefix))
   return _PARSER
