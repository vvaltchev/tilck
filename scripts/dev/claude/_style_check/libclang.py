# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-function-docstring, bad-indentation
"""
What style_check needs of libclang beyond the Python bindings.

The `clang` bindings are a separate package with their own copy of
libclang's tables. When they are older than the library, a cursor of a
kind they lack makes `cursor.kind` raise ValueError (bindings 18 do not
know kind 280, BuiltinBitCastExpr, which libclang 18 returns for
`__builtin_bit_cast`). Nothing here depends on the two versions
matching.
"""


def kind_of(cursor):
   """The cursor's kind, or None when the bindings cannot name it.

   A rule can only look for kinds the bindings define, so a kind they
   lack is never one a rule wants: None compares unequal to all of
   them. Every read of a cursor's kind goes through here."""

   try:
      return cursor.kind
   except ValueError:
      return None
