# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-function-docstring, bad-indentation
# pylint: disable=too-many-instance-attributes, too-few-public-methods
"""
Parsing for style_check.

A file is parsed only with the compile commands of the configurations
that compile it -- as the main file, or through an #include -- and
every parse is checked against the compiler that runs that command:

  - libclang must report no error, anywhere in the translation unit;

  - in the file, every #if branch must be taken by libclang exactly
    when the compiler takes it (see configs.py for how the compiler's
    branches are read). Two kinds of branch may differ, because
    libclang is clang and style_check is a tool: those asking which
    compiler reads the code (__clang__, __GNUC__, __has_builtin ...)
    and those on CLANGD, which style_check defines as clangd does.
    There, a difference that hides code the compiler builds is a
    warning; any other difference is an error.

A file's branches can depend on the configuration (an #ifdef on the
arch, on a build option) or on what the including file defines. The
parses of a file are chosen so that, together, they take every branch
that some compilation takes: usually one, more when no single
compilation reaches every branch. A branch no compilation takes is
code no configuration builds; it is reported, not parsed.

Nothing here falls back to a guess: a file no configuration compiles,
a compiler that fails, a libclang error or a divergence is a Problem,
and a Problem fails the check.
"""

import os
import re
import functools
import subprocess

from collections import Counter, OrderedDict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

import clang.cindex as ci

from . import configs as _configs
from . import conditionals as _cond
from . import libclang as _lc


def find_repo_root() -> Optional[Path]:

   try:
      out = subprocess.check_output(
         ['git', 'rev-parse', '--show-toplevel'],
         stderr=subprocess.DEVNULL,
         text=True
      ).strip()
      return Path(out)
   except (subprocess.CalledProcessError, FileNotFoundError):
      return None


ERROR = 'error'
WARNING = 'warning'

SOURCE_SUFFIXES = ('.c', '.h', '.cpp', '.hpp', '.cc')


def project_predicate(repo_root: Optional[Path], configs):
   """Whether a path (real) is one of the checkout's own sources, as
   opposed to a system header, a toolchain header or a header a build
   generated: under the repository, outside every directory git
   ignores at its top level (build trees, the toolchain) and outside
   every configuration's build tree."""

   roots = [str(c.root) + os.sep for c in configs]
   ignored = []

   if repo_root is not None:

      try:
         out = subprocess.check_output(
            ['git', 'ls-files', '--others', '--ignored',
             '--exclude-standard', '--directory'],
            cwd=str(repo_root), stderr=subprocess.DEVNULL, text=True,
         )
      except (subprocess.CalledProcessError, FileNotFoundError):
         out = ''

      for line in out.splitlines():
         if line.endswith('/') and line.count('/') == 1:
            ignored.append(os.path.join(str(repo_root), line))

   top = str(repo_root) + os.sep if repo_root is not None else None

   def is_project(path: str) -> bool:
      return (
         path.endswith(SOURCE_SUFFIXES) and
         (top is None or path.startswith(top)) and
         not any(path.startswith(d) for d in ignored + roots)
      )

   return is_project


@dataclass
class Problem:

   kind: str            # not_compiled, compiler_failed, load_failed,
                        # clang_error, divergence, compiler_only_code,
                        # unknown_kind
   severity: str
   line: int            # line in the checked file, 0 if none
   message: str
   where: List[str] = field(default_factory=list)   # the parses


@dataclass
class Parse:

   entry: _configs.Entry
   tu: object


@dataclass
class FileParses:

   path: str
   parses: List[Parse] = field(default_factory=list)
   problems: List[Problem] = field(default_factory=list)

   # Regions (conditionals.Region) of the file that no C/C++
   # compilation takes: `asm_only` are taken by an assembler file
   # only, `dead` by no compilation at all.
   asm_only: list = field(default_factory=list)
   dead: list = field(default_factory=list)

   # Branches asking which compiler reads the code (or naming
   # CLANGD), where libclang differs from the compiler without hiding
   # any of the compiler's code: (region, entry).
   intended: list = field(default_factory=list)

   # Compiler flags libclang refused, per parse.
   dropped: set = field(default_factory=set)

   @property
   def failed(self) -> bool:
      return any(p.severity == ERROR for p in self.problems)

   def add(self, kind, severity, line, message, where=None):
      """Record a problem; one already seen in another parse only
      gains that parse in its `where`."""

      for known in self.problems:
         if (known.kind, known.severity, known.line, known.message) == \
            (kind, severity, line, message):
            break
      else:
         known = Problem(kind, severity, line, message)
         self.problems.append(known)

      if where and where not in known.where:
         known.where.append(where)


class Parser:

   TU_CACHE_SIZE = 8
   MAX_ERRORS_SHOWN = 5

   def __init__(self, configs: List[_configs.Config], is_project):

      self.configs = configs
      self.is_project = is_project
      self.index = ci.Index.create()
      self._regions: Dict[str, list] = {}
      self._views = None
      self._tus = OrderedDict()
      self._includes: Dict[str, Counter] = {}

   # -- the compiler's side --------------------------------------------

   def regions(self, path: str) -> list:

      r = self._regions.get(path)

      if r is None:

         with open(path, 'r', encoding='utf-8', errors='replace') as fh:
            r = _cond.scan(fh.read())

         self._regions[path] = r

      return r

   def _active_of(self, path: str, lines: set) -> frozenset:
      return frozenset(
         r.id for r in self.regions(path) if r.observable & lines
      )

   def views(self):
      """(entry, view) for every compilation of every configuration."""

      if self._views is None:

         self._views = []

         for cfg in self.configs:

            cache = _configs.ViewCache(cfg, self.is_project, self._active_of)
            got = cache.load_all()

            for e in cfg.entries:
               self._views.append((e, got[e.key]))

      return self._views

   # -- libclang's side ------------------------------------------------

   def _tu(self, entry: _configs.Entry):

      tu = self._tus.get(entry.key)

      if tu is not None:
         self._tus.move_to_end(entry.key)
         return tu

      pa = _configs.libclang_args(entry)

      try:
         tu = self.index.parse(
            entry.file,
            args=list(pa.args),
            options=ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
         )
      except ci.TranslationUnitLoadError:
         tu = None

      self._tus[entry.key] = tu
      self._includes.pop(entry.key, None)

      while len(self._tus) > self.TU_CACHE_SIZE:
         old, _ = self._tus.popitem(last=False)
         self._includes.pop(old, None)

      return tu

   def _inclusions(self, entry, tu, path: str) -> int:
      """How many times the translation unit enters `path`."""

      counts = self._includes.get(entry.key)

      if counts is None:

         counts = Counter()
         counts[os.path.realpath(entry.file)] += 1

         for inc in tu.get_includes():
            counts[os.path.realpath(inc.include.name)] += 1

         self._includes[entry.key] = counts

      return counts[path]

   def clang_active(self, entry, tu, path: str) -> frozenset:
      """The regions of `path` libclang takes in this parse. A file
      entered n times has its untaken branches skipped once per entry
      that skips them: a branch is taken if some entry takes it."""

      skipped = _lc.skipped_lines(tu, path)
      times = self._inclusions(entry, tu, path)
      active = set()

      for r in self.regions(path):

         if not r.observable:
            continue

         first = min(r.observable)
         n = sum(1 for a, b in skipped if a <= first <= b)

         if n < times:
            active.add(r.id)

      return frozenset(active)

   # -- one file -------------------------------------------------------

   def parse_file(self, path: str, check_kinds: bool = False) -> FileParses:

      path = os.path.realpath(path)
      out = FileParses(path)
      regions = [r for r in self.regions(path) if r.observable]
      by_id = _cond.by_id(self.regions(path))

      cands = []
      asm_taken = set()
      taken = set()
      failed = []

      for e, v in self.views():

         if v.error and e.file == path:
            failed.append((e, v))
            continue

         if path not in v.active:
            continue

         if e.is_asm:
            asm_taken |= v.active[path]
         else:
            cands.append((e, v))
            taken |= v.active[path]

      if not cands:

         if failed:
            e, v = failed[0]
            out.add('compiler_failed', ERROR, 0,
                    'the compiler cannot compile it: {}'.format(v.error),
                    _describe(e, self.configs))
         elif asm_taken:
            out.asm_only = regions
         else:
            out.add('not_compiled', ERROR, 0,
                    'no configuration compiles it: none of {} compiles it '
                    'or a file that includes it'.format(
                       ', '.join(c.name for c in self.configs) or '(none)'
                    ))

         return out

      parsed = set()

      for e, v in self._choose(path, cands, taken):
         parsed |= self._parse_one(out, path, e, v, by_id, check_kinds)

      # What no parse covers: a branch only assemblers compile, or one
      # nothing compiles. A branch only libclang takes (clang's side of
      # an #ifdef __clang__) is covered.
      covered = taken | parsed
      out.asm_only = [
         r for r in regions if r.id in asm_taken and r.id not in covered
      ]
      out.dead = [
         r for r in regions if r.id not in covered | asm_taken
      ]

      return out

   def _choose(self, path: str, cands, needed: set):
      """A small set of compilations that together take every region
      of `path` that some compilation takes. Greedy: the compilation
      taking the most regions still missing, preferring one already
      parsed, then one where `path` is the main file, then the order
      of the configurations."""

      order = {c.name: i for i, c in enumerate(self.configs)}
      missing = set(needed)
      chosen = []

      def rank(item):
         e, v = item
         return (
            -len(v.active[path] & missing),
            e.key not in self._tus,
            e.file != path,
            order[e.config],
            e.file,
         )

      while True:

         best = min(cands, key=rank)
         chosen.append(best)
         missing -= best[1].active[path]

         if not missing:
            break

      return chosen

   def _parse_one(self, out, path, e, v, by_id, check_kinds) -> frozenset:
      """Parse `path` with the command of `e`, check the parse, and
      return the regions of `path` it covers."""

      tu = self._tu(e)
      where = _describe(e, self.configs)

      if tu is None:
         out.add('load_failed', ERROR, 0, 'libclang could not parse it',
                 where)
         return frozenset()

      out.parses.append(Parse(e, tu))
      out.dropped |= _configs.libclang_args(e).dropped
      shown = 0

      for d in tu.diagnostics:

         if d.severity < ci.Diagnostic.Error:
            continue

         if shown < self.MAX_ERRORS_SHOWN:

            f = d.location.file
            in_file = f is not None and os.path.realpath(f.name) == path
            loc = '{}:{}'.format(_short(f.name), d.location.line) if f \
               else 'no location'

            out.add('clang_error', ERROR, d.location.line if in_file else 0,
                    'libclang: {} [{}]'.format(d.spelling, loc), where)

         shown += 1

      if shown > self.MAX_ERRORS_SHOWN:
         out.add('clang_error', ERROR, 0, '... and {} more libclang errors'
                 .format(shown - self.MAX_ERRORS_SHOWN), where)

      gcc = v.active[path]
      clang = self.clang_active(e, tu, path)

      for rid in sorted(gcc ^ clang):

         r = by_id[rid]
         cond = r.conditions[-1] if r.conditions else 'the top level'

         if not _compiler_specific(by_id, rid):

            who, other = ('the compiler', 'libclang') if rid in gcc \
               else ('libclang', 'the compiler')

            out.add('divergence', ERROR, rid,
                    '{} takes the branch at line {} (`{}`), {} does not'
                    .format(who, rid, cond, other), where)

         elif rid in gcc and r.code:

            out.add('compiler_only_code', WARNING, rid,
                    'only the compiler takes the branch at line {} (`{}`): '
                    'libclang, being clang, cannot parse its code as the '
                    'compiler does'.format(rid, cond), where)

         else:
            out.intended.append((r, e))

      if check_kinds:
         self._check_kinds(out, tu, path)

      return clang

   def _check_kinds(self, out, tu, path):
      """Cursors of the file whose kind the Python bindings cannot
      name: the rules cannot see them (see libclang.kind_of)."""

      seen = set()

      for c in tu.cursor.walk_preorder():

         if _lc.kind_of(c) is not None:
            continue

         f = c.location.file

         if f is None or os.path.realpath(f.name) != path:
            continue

         k = _lc.kind_spelling(c)

         if k not in seen:
            seen.add(k)
            out.add('unknown_kind', WARNING, c.location.line,
                    'the clang bindings do not know cursor kind {} ({}); '
                    'rules cannot see it -- newer bindings would'
                    .format(c._kind_id, k))  # pylint: disable=protected-access


# Conditions that ask which compiler is reading the code. libclang is
# clang, and answers them as clang: such branches may differ from the
# compiler's. So may the ones on the tooling macro, by definition.
_COMPILER_SPECIFIC_RE = re.compile(
   r'\b(?:{}|__clang\w*__|__GNUC\w*__|__has_(?:builtin|attribute|feature|'
   r'extension|cpp_attribute|c_attribute|declspec_attribute))\b'
   .format(_configs.TOOLING_MACRO)
)


def _compiler_specific(by_id, rid) -> bool:
   """Whether the region, or one enclosing it, is a branch of a group
   whose conditions ask which compiler reads the code."""

   return any(
      _COMPILER_SPECIFIC_RE.search(text)
      for r in _cond.ancestors(by_id, rid)
      for text in r.conditions
   )


def _describe(e, configs) -> str:
   """Which compilation a parse is: its configuration, the file it
   compiles and where in the build tree it runs."""

   root = next(c.root for c in configs if c.name == e.config)

   try:
      sub = os.path.relpath(e.directory, root)
   except ValueError:
      sub = e.directory

   return '{}: {} in {}'.format(e.config, _short(e.file), sub)


@functools.lru_cache(maxsize=None)
def _repo_root():
   return find_repo_root()


def _short(path: str) -> str:

   root = _repo_root()

   if root is not None:
      try:
         return str(Path(path).relative_to(root))
      except ValueError:
         pass

   return path
