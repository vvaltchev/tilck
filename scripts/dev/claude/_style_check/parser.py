# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-module-docstring, missing-class-docstring
# pylint: disable=missing-function-docstring, broad-except

import json

from pathlib import Path
from typing import List, Optional

import clang.cindex


def find_repo_root() -> Optional[Path]:

   import subprocess

   try:
      out = subprocess.check_output(
         ['git', 'rev-parse', '--show-toplevel'],
         stderr=subprocess.DEVNULL,
         text=True
      ).strip()
      return Path(out)
   except (subprocess.CalledProcessError, FileNotFoundError):
      return None


def resolve_build_dir(arg: str) -> Path:

   p = Path(arg)

   if p.is_absolute():
      return p

   root = find_repo_root()

   if root is None:
      return Path.cwd() / p

   return root / p


class Parser:

   def __init__(self, build_dir: Optional[Path]):

      self.build_dir = build_dir
      self.compile_db = None
      self._proxy_args_c = None
      self._proxy_args_cpp = None

      if build_dir and (build_dir / 'compile_commands.json').exists():

         try:
            self.compile_db = \
               clang.cindex.CompilationDatabase.fromDirectory(str(build_dir))
         except clang.cindex.CompilationDatabaseError:
            self.compile_db = None

      self.index = clang.cindex.Index.create()

   def has_entry(self, file_path: Path) -> bool:

      if self.compile_db is None:
         return False

      cmds = list(
         self.compile_db.getCompileCommands(str(file_path.resolve()))
      )
      return len(cmds) > 0

   def parse(self, file_path: Path):
      """Parse a file. Returns a clang TranslationUnit, or None."""

      args = self._args_for(file_path)

      if args is None:
         return None

      try:
         tu = self.index.parse(str(file_path), args=args)
      except clang.cindex.TranslationUnitLoadError:
         return None

      return tu

   def _args_for(self, file_path: Path) -> Optional[List[str]]:

      if self.compile_db is None:
         return None

      cmds = list(
         self.compile_db.getCompileCommands(str(file_path.resolve()))
      )

      if cmds:
         return self._clean_args(list(cmds[0].arguments))

      # No direct entry (typical for headers and test fixtures). Use a
      # proxy set of args taken from any source file of the right kind
      # in the build DB.
      want_cpp = file_path.suffix in ('.cpp', '.hpp')

      if want_cpp:

         if self._proxy_args_cpp is None:
            self._proxy_args_cpp = self._build_proxy_args(cpp=True)

         return self._proxy_args_cpp

      if self._proxy_args_c is None:
         self._proxy_args_c = self._build_proxy_args(cpp=False)

      return self._proxy_args_c

   @staticmethod
   def _clean_args(args: list) -> List[str]:

      # Drop the compiler name (args[0]).
      result = list(args)[1:]

      out = []
      i = 0
      n = len(result)

      while i < n:

         a = result[i]

         if a == '-c':
            i += 1
            continue

         if a == '-o':
            i += 2  # skip flag and its argument
            continue

         # Skip input source files
         if a.endswith('.c') or a.endswith('.S') or a.endswith('.cpp'):
            i += 1
            continue

         out.append(a)
         i += 1

      return out

   def _build_proxy_args(self, cpp: bool = False) -> Optional[List[str]]:
      """Return a clean argv from any entry in the DB. When `cpp` is
      True, prefer a .cpp entry (host-side gtest build) so libclang
      gets `-std=gnu++NN` and friends; otherwise prefer a kernel .c
      entry."""

      if self.build_dir is None:
         return None

      cdb_file = self.build_dir / 'compile_commands.json'

      if not cdb_file.exists():
         return None

      try:
         with open(cdb_file, 'r', encoding='utf-8') as fh:
            entries = json.load(fh)
      except (IOError, json.JSONDecodeError):
         return None

      preferred = None
      fallback = None

      if cpp:
         wanted_suffix = '.cpp'
      else:
         wanted_suffix = '.c'

      for entry in entries:

         f = entry.get('file', '')

         if not f.endswith(wanted_suffix):
            continue

         args = entry.get('arguments')

         if args is None:
            cmd = entry.get('command', '')
            args = cmd.split()

         if cpp:

            if '/tests/unit/' in f and preferred is None:
               preferred = args

         else:

            if '/kernel/' in f and preferred is None:
               preferred = args

         if fallback is None:
            fallback = args

      chosen = preferred if preferred is not None else fallback

      if chosen is None:
         return None

      return self._clean_args(chosen)
