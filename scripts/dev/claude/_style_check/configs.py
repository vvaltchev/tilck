# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-function-docstring, bad-indentation
# pylint: disable=too-many-instance-attributes, too-few-public-methods
"""
Build configurations: where style_check learns how every file is
really compiled.

A configuration is a Tilck build tree. Its compile_db/compile_commands
.json (merged at the end of every build) lists each compilation with
its exact command. style_check parses a file only with a command that
compiles it, or that compiles a file including it: nothing is guessed,
and nothing meant for another arch or another build option is parsed
with the wrong flags.

For each command, two questions go to the compiler named in it:

  - its environment: the target machine (-dumpmachine), its system
    include directories (-E -v) and its default language standard
    (-dM -E), so that libclang reads the same headers, in the same
    language, as the compiler does;

  - its view (`-E -fdirectives-only -dI`): which files the compilation
    reads and, per line, what survives preprocessing. With
    -fdirectives-only no macro is expanded, so every line of a branch
    the compiler takes leaves a trace at its own line number, and the
    branch each #if takes in every file follows from the output
    (see conditionals.py).

Views are cached per configuration, in <build>/style_check/, and
recomputed when a command or any file it reads changes.
"""

import os
import re
import json
import shlex
import hashlib
import functools
import subprocess

from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

import clang.cindex as ci


# Tilck's macro for "a clang-based tool, not the compiler, is reading
# this code" (see .clangd). Code that only the compiler can digest,
# like GCC's <ia32intrin.h>, sits behind `#ifndef CLANGD`.
TOOLING_MACRO = 'CLANGD'

LANG_BY_SUFFIX = {
   '.c': 'c',
   '.cpp': 'c++',
   '.cc': 'c++',
   '.cxx': 'c++',
   '.S': 'assembler-with-cpp',
}

VIEW_CACHE_VERSION = 1


class ConfigError(Exception):
   pass


@dataclass(frozen=True)
class Entry:
   """One compilation of a configuration."""

   config: str                   # the configuration's name
   directory: str                # where the compiler runs
   file: str                     # real path of the file compiled
   compiler: str
   flags: Tuple[str, ...]        # without -c, -o, -M* and the input
   lang: str                     # 'c', 'c++' or 'assembler-with-cpp'
   key: str = field(compare=False, default='')

   @property
   def is_asm(self) -> bool:
      return self.lang == 'assembler-with-cpp'


# Options that take a separate argument, and that say nothing about
# the code: what the compiler writes and where.
_IO_ARG = ('-o', '-MF', '-MT', '-MQ')
_IO_FLAG = ('-c', '-MD', '-MMD', '-MP', '-M', '-MM')


def _strip_io(argv: List[str], directory: str, file: str) -> Tuple[str, ...]:

   out = []
   i = 0

   while i < len(argv):

      a = argv[i]

      if a in _IO_ARG:
         i += 2
         continue

      # A bare I/O flag, or an I/O option joined to its argument
      # (`-ofoo.o`, `-MFfoo.d`).
      if a in _IO_FLAG or any(a.startswith(o) for o in _IO_ARG):
         i += 1
         continue

      if not a.startswith('-') and \
         os.path.realpath(os.path.join(directory, a)) == file:
         i += 1
         continue

      out.append(a)
      i += 1

   return tuple(out)


def _entry_key(directory, file, compiler, flags) -> str:
   blob = json.dumps([directory, file, compiler, list(flags)])
   return hashlib.sha1(blob.encode('utf-8')).hexdigest()


class Config:
   """A build tree and the compilations its compile DB lists."""

   def __init__(self, name: str, root: Path, db_dir: Path):

      self.name = name
      self.root = root
      self.db_dir = db_dir
      self.entries = self._load()

   def _load(self) -> List[Entry]:

      path = self.db_dir / 'compile_commands.json'

      try:
         with open(path, 'r', encoding='utf-8') as fh:
            data = json.load(fh)
      except (OSError, ValueError) as e:
         raise ConfigError('cannot read {}: {}'.format(path, e)) from e

      out = []

      for e in data:

         directory = e['directory']
         file = os.path.realpath(os.path.join(directory, e['file']))
         lang = LANG_BY_SUFFIX.get(os.path.splitext(file)[1])

         if lang is None:
            continue

         argv = e.get('arguments') or shlex.split(e['command'])
         flags = _strip_io(argv[1:], directory, file)
         key = _entry_key(directory, file, argv[0], flags)
         out.append(Entry(self.name, directory, file, argv[0], flags,
                          lang, key))

      return out


def resolve_config(arg: str, repo_root: Optional[Path]) -> Config:
   """A configuration from a --build-dir argument: a build tree, or
   its compile_db/ directory."""

   p = Path(arg)

   if not p.is_absolute() and not p.exists() and repo_root is not None:
      p = repo_root / arg

   p = p.resolve()

   if (p / 'compile_commands.json').exists() and p.name == 'compile_db':
      root, db_dir = p.parent, p
   elif (p / 'compile_db' / 'compile_commands.json').exists():
      root, db_dir = p, p / 'compile_db'
   else:
      raise ConfigError(
         '{}: no compile_db/compile_commands.json (build it first)'
         .format(arg)
      )

   return Config(_display(root, repo_root), root, db_dir)


def discover(repo_root: Path) -> List[Path]:
   """The build trees in the checkout: build/ first, then every
   build-*/, each only if it has a compile DB."""

   found = []
   candidates = [repo_root / 'build'] + sorted(repo_root.glob('build-*'))

   for d in candidates:
      if (d / 'compile_db' / 'compile_commands.json').exists():
         found.append(d)

   return found


def _display(p: Path, repo_root: Optional[Path]) -> str:

   if repo_root is not None:
      try:
         return str(p.relative_to(repo_root))
      except ValueError:
         pass

   return str(p)


# ---------------------------------------------------------------------
# The compiler's environment
# ---------------------------------------------------------------------

@dataclass(frozen=True)
class CompilerEnv:
   machine: str                  # target triple, from -dumpmachine
   system_dirs: Tuple[str, ...]  # its built-in #include <...> path
   std: Optional[str]            # its default -std, if recognized


# Flags that do not shape the compiler's built-in environment: include
# paths, macros and warnings. Dropped from the environment queries so
# that one query serves every command of a build target.
_ENV_DROP_ARG = ('-I', '-isystem', '-iquote', '-idirafter', '-include',
                 '-imacros', '-D', '-U')


def env_flags(flags: Tuple[str, ...]) -> Tuple[str, ...]:

   out = []
   i = 0

   while i < len(flags):

      a = flags[i]

      if a in _ENV_DROP_ARG:
         i += 2
         continue

      if a.startswith(_ENV_DROP_ARG) or a.startswith('-W') or a == '-w':
         i += 1
         continue

      out.append(a)
      i += 1

   return tuple(out)


@functools.lru_cache(maxsize=None)
def compiler_env(compiler: str, eflags: Tuple[str, ...], lang: str,
                 directory: str) -> CompilerEnv:

   def run(extra):
      return subprocess.run(
         [compiler, *eflags, *extra],
         cwd=directory,
         capture_output=True,
         text=True,
         check=False,
      )

   machine = run(['-dumpmachine']).stdout.strip()
   verbose = run(['-x', lang, '-E', '-v', os.devnull, '-o', os.devnull])
   macros = run(['-x', lang, '-dM', '-E', os.devnull]).stdout
   lines = verbose.stderr.splitlines()

   try:
      a = lines.index('#include <...> search starts here:')
      b = lines.index('End of search list.')
   except ValueError as e:
      raise ConfigError(
         '{}: cannot read its include search path'.format(compiler)
      ) from e

   dirs = tuple(
      os.path.normpath(x.strip().replace(' (framework directory)', ''))
      for x in lines[a + 1:b]
   )

   return CompilerEnv(machine, dirs, _std_of(macros, lang))


_C_STD = {
   '199901': '99', '201112': '11', '201710': '17',
   '202000': '2x', '202311': '23',
}
_CXX_STD = {
   '201103': '11', '201402': '14', '201703': '17',
   '202002': '20', '202100': '2b', '202302': '23',
}


def _std_of(macros: str, lang: str) -> Optional[str]:

   defs = {}

   for line in macros.splitlines():

      parts = line.split(' ', 2)

      if len(parts) == 3 and parts[0] == '#define':
         defs[parts[1]] = parts[2]

   strict = '__STRICT_ANSI__' in defs

   if lang == 'c':
      ver = _C_STD.get(defs.get('__STDC_VERSION__', '').rstrip('L'))
      return ('c' if strict else 'gnu') + ver if ver else None

   ver = _CXX_STD.get(defs.get('__cplusplus', '').rstrip('L'))
   return ('c++' if strict else 'gnu++') + ver if ver else None


# ---------------------------------------------------------------------
# libclang's arguments for an entry
# ---------------------------------------------------------------------

_REJECTED_RE = (
   re.compile(r"unknown argument: '([^']+)'"),
   re.compile(r"unsupported option '([^']+)' for target"),
)


@functools.lru_cache(maxsize=None)
def rejected_flags(machine: str, lang: str,
                   flags: Tuple[str, ...]) -> frozenset:
   """The compiler's flags that libclang refuses (GCC-only options such
   as -maccumulate-outgoing-args), found by asking libclang itself:
   parse an empty file with the flags and read its complaints."""

   probe = '/style_check/probe' + ('.cpp' if lang == 'c++' else '.c')
   index = ci.Index.create()
   rejected = set()

   for _ in range(4):

      args = ['--target=' + machine, '-x', lang]
      args += [f for f in flags if f not in rejected]

      try:
         tu = index.parse(probe, args=args, unsaved_files=[(probe, '')])
      except ci.TranslationUnitLoadError:
         break

      found = set()

      for d in tu.diagnostics:
         for rx in _REJECTED_RE:
            m = rx.search(d.spelling)
            if m:
               found.add(m.group(1))

      if not found - rejected:
         break

      rejected |= found

   return frozenset(rejected)


@dataclass(frozen=True)
class ParseArgs:
   args: Tuple[str, ...]
   dropped: frozenset            # compiler flags libclang refused


def libclang_args(entry: Entry) -> ParseArgs:
   """The arguments that make libclang see what the compiler of
   `entry` sees: the same target, the same system headers (and only
   those: -nostdinc), the same language standard, the same flags."""

   env = compiler_env(entry.compiler, env_flags(entry.flags), entry.lang,
                      entry.directory)

   flags = [f for f in entry.flags if not f.startswith('-Werror')]
   has_std = any(f.startswith('-std=') for f in flags)
   dropped = rejected_flags(env.machine, entry.lang, tuple(flags))

   args = ['--target=' + env.machine, '-x', entry.lang, '-nostdinc', '-w']
   args += [f for f in flags if f not in dropped]

   if not has_std and env.std:
      args.append('-std=' + env.std)

   for d in env.system_dirs:
      args += ['-isystem', d]

   args.append('-D' + TOOLING_MACRO)
   return ParseArgs(tuple(args), dropped)


# ---------------------------------------------------------------------
# The compiler's view
# ---------------------------------------------------------------------

@dataclass
class View:
   """What one compilation reads, per the compiler."""

   deps: List[str]                     # real paths, every file read
   active: Dict[str, frozenset]        # project file -> region ids
   error: Optional[str] = None         # the compiler failed


_MARKER_RE = re.compile(r'^# (\d+) "((?:[^"\\]|\\.)*)"')


def _unescape(name: str) -> str:
   return re.sub(r'\\(.)', r'\1', name)


def evidence(output: str, directory: str) -> Dict[str, set]:
   """Per file (real path), the lines that left a trace in the output
   of `-E -fdirectives-only`."""

   ev = {}
   resolved = {}
   cur = None
   line = 0

   for text in output.split('\n'):

      m = _MARKER_RE.match(text)

      if m:

         name = m.group(2)
         line = int(m.group(1))

         if name.startswith('<'):
            cur = None
            continue

         cur = resolved.get(name)

         if cur is None:
            cur = os.path.realpath(os.path.join(directory, _unescape(name)))
            resolved[name] = cur

         ev.setdefault(cur, set())
         continue

      if cur is not None and text.strip():
         ev[cur].add(line)

      line += 1

   return ev


def preprocess(entry: Entry) -> subprocess.CompletedProcess:
   return subprocess.run(
      [entry.compiler, *entry.flags, '-E', '-fdirectives-only', '-dI',
       entry.file],
      cwd=entry.directory,
      capture_output=True,
      text=True,
      errors='replace',
      check=False,
   )


def compute_view(entry: Entry, is_project: Callable[[str], bool],
                 active_of: Callable[[str, set], frozenset]) -> View:

   res = preprocess(entry)

   if res.returncode != 0:
      first = (res.stderr.strip().splitlines() or ['(no output)'])[0]
      return View([entry.file], {}, error=first)

   ev = evidence(res.stdout, entry.directory)
   active = {
      f: active_of(f, lines) for f, lines in ev.items() if is_project(f)
   }
   return View(sorted(ev), active)


class ViewCache:
   """The compiler's views of a configuration's compilations, kept in
   <build>/style_check/views.json. An entry's view is valid while its
   command is the same and none of the files it read has changed."""

   def __init__(self, config: Config, is_project, active_of):

      self.config = config
      self.is_project = is_project
      self.active_of = active_of
      self.path = config.root / 'style_check' / 'views.json'
      self.views: Dict[str, View] = {}
      self.stamps: Dict[str, int] = {}

   def load_all(self) -> Dict[str, View]:
      """Every entry's view, computing (in parallel) the missing and
      stale ones and saving the cache if anything changed."""

      cached, stamps = self._read()
      mtimes = {}
      todo = []

      for e in self.config.entries:

         v = cached.get(e.key)

         if v is not None and _stamp(v.deps, mtimes) == stamps.get(e.key):
            self.views[e.key] = v
            self.stamps[e.key] = stamps[e.key]
         else:
            todo.append(e)

      if todo:

         workers = max(1, os.cpu_count() or 1)

         with ThreadPoolExecutor(max_workers=workers) as pool:
            done = pool.map(
               lambda e: (e, compute_view(e, self.is_project,
                                          self.active_of)),
               todo,
            )

            for e, v in done:
               self.views[e.key] = v
               self.stamps[e.key] = _stamp(v.deps, {})

         self._write()

      return self.views

   def _read(self):

      try:
         with open(self.path, 'r', encoding='utf-8') as fh:
            data = json.load(fh)
      except (OSError, ValueError):
         return {}, {}

      if data.get('version') != VIEW_CACHE_VERSION:
         return {}, {}

      paths = data['paths']
      views = {}
      stamps = {}

      for key, v in data['entries'].items():
         views[key] = View(
            [paths[i] for i in v['deps']],
            {paths[int(i)]: frozenset(ids) for i, ids in v['active'].items()},
            v['error'],
         )
         stamps[key] = v['stamp']

      return views, stamps

   def _write(self):

      paths = []
      index = {}

      def idx(p):
         if p not in index:
            index[p] = len(paths)
            paths.append(p)
         return index[p]

      entries = {}

      for key, v in self.views.items():
         entries[key] = {
            'stamp': self.stamps[key],
            'deps': [idx(p) for p in v.deps],
            'active': {
               str(idx(p)): sorted(ids) for p, ids in v.active.items()
            },
            'error': v.error,
         }

      data = {'version': VIEW_CACHE_VERSION, 'paths': paths,
              'entries': entries}

      self.path.parent.mkdir(parents=True, exist_ok=True)
      tmp = self.path.with_suffix('.tmp.{}'.format(os.getpid()))

      with open(tmp, 'w', encoding='utf-8') as fh:
         json.dump(data, fh)

      os.replace(tmp, self.path)


def _stamp(paths: List[str], mtimes: Dict[str, int]) -> Optional[int]:
   """The newest modification time among `paths`, or None if one of
   them is gone."""

   newest = 0

   for p in paths:

      t = mtimes.get(p)

      if t is None:

         try:
            t = os.stat(p).st_mtime_ns
         except OSError:
            return None

         mtimes[p] = t

      newest = max(newest, t)

   return newest
