# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-function-docstring, bad-indentation
# pylint: disable=protected-access, too-few-public-methods
"""
What style_check needs of libclang beyond the Python bindings.

The `clang` bindings are a separate package with their own copy of
libclang's tables. When they are older than the library, a cursor of a
kind they lack makes `cursor.kind` raise ValueError (bindings 18 do not
know kind 280, BuiltinBitCastExpr, which libclang 18 returns for
`__builtin_bit_cast`). Nothing here depends on the two versions
matching: kind_of() tolerates a kind the bindings lack, and the rest
asks the library itself, through functions the bindings do not wrap.
"""

import ctypes

import clang.cindex as ci


def kind_of(cursor):
   """The cursor's kind, or None when the bindings cannot name it.

   A rule can only look for kinds the bindings define, so a kind they
   lack is never one a rule wants: None compares unequal to all of
   them. Every read of a cursor's kind goes through here."""

   try:
      return cursor.kind
   except ValueError:
      return None


def kind_spelling(cursor) -> str:
   """libclang's own name for the cursor's kind, e.g. 'IfStmt' --
   known even to bindings that lack the kind."""

   fn = _fn('clang_getCursorKindSpelling', [ctypes.c_uint], ci._CXString)
   return ci._CXString.from_result(fn(cursor._kind_id))


class _SourceRangeList(ctypes.Structure):
   _fields_ = [
      ('count', ctypes.c_uint),
      ('ranges', ctypes.POINTER(ci.SourceRange)),
   ]


def skipped_lines(tu, path: str) -> list:
   """The (first, last) line ranges of `path` that the preprocessor
   skipped in this translation unit (the bodies of the conditional
   branches it did not take), one per inclusion that skipped them.
   Needs a translation unit parsed with
   PARSE_DETAILED_PROCESSING_RECORD."""

   get = _fn(
      'clang_getSkippedRanges',
      [ci.TranslationUnit, ci.File],
      ctypes.POINTER(_SourceRangeList),
   )
   dispose = _fn(
      'clang_disposeSourceRangeList',
      [ctypes.POINTER(_SourceRangeList)],
      None,
   )

   lst = get(tu, tu.get_file(path))

   try:
      return [
         (lst.contents.ranges[i].start.line, lst.contents.ranges[i].end.line)
         for i in range(lst.contents.count)
      ]
   finally:
      dispose(lst)


_FUNCS = {}


def _fn(name, argtypes, restype):

   fn = _FUNCS.get(name)

   if fn is None:
      fn = getattr(ci.conf.lib, name)
      fn.argtypes = argtypes
      fn.restype = restype
      _FUNCS[name] = fn

   return fn
