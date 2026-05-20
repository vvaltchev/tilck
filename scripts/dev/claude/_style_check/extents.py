# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-module-docstring, missing-class-docstring
# pylint: disable=missing-function-docstring

from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

import clang.cindex
from clang.cindex import CursorKind


@dataclass
class Extent:

   kind: str  # CursorKind name as string

   # cursor.extent (full extent including body for definitions)
   start_line: int
   start_col: int
   end_line: int
   end_col: int

   # cursor.location (typically the name position)
   name_line: int
   name_col: int

   spelling: str
   is_definition: bool
   storage_class: Optional[str]  # 'static', 'extern', 'none', ...


# Cursor kinds we care about for v1 rules.
RELEVANT_KINDS = frozenset({
   CursorKind.FUNCTION_DECL,
   CursorKind.FUNCTION_TEMPLATE,
   CursorKind.IF_STMT,
   CursorKind.FOR_STMT,
   CursorKind.WHILE_STMT,
   CursorKind.DO_STMT,
   CursorKind.SWITCH_STMT,
   CursorKind.COMPOUND_STMT,
   CursorKind.STRUCT_DECL,
   CursorKind.UNION_DECL,
   CursorKind.ENUM_DECL,
   CursorKind.ENUM_CONSTANT_DECL,
   CursorKind.VAR_DECL,
   CursorKind.PARM_DECL,
   CursorKind.RETURN_STMT,
   CursorKind.GOTO_STMT,
   CursorKind.LABEL_STMT,
   CursorKind.CALL_EXPR,
   CursorKind.CSTYLE_CAST_EXPR,
   CursorKind.INIT_LIST_EXPR,
})


def walk_main_file(tu, file_path: Path) -> List[Extent]:

   if tu is None:
      return []

   out = []
   wanted = str(file_path.resolve())

   for cursor in tu.cursor.walk_preorder():

      if cursor.kind not in RELEVANT_KINDS:
         continue

      loc = cursor.location

      if loc.file is None:
         continue

      # cursor.location.file is the SPELLING location's file. For a
      # cursor that came from a macro expansion, this is where the user
      # wrote the macro CALL, not where the macro body lives. We compare
      # against the file under analysis to keep only in-file constructs.
      if str(Path(str(loc.file)).resolve()) != wanted:
         continue

      ext = cursor.extent
      sc = getattr(cursor, 'storage_class', None)
      sc_name = sc.name.lower() if sc is not None else None

      is_def = False

      if cursor.kind.is_declaration():
         try:
            is_def = cursor.is_definition()
         except Exception:  # pylint: disable=broad-except
            is_def = False

      out.append(Extent(
         kind=cursor.kind.name,
         start_line=ext.start.line,
         start_col=ext.start.column,
         end_line=ext.end.line,
         end_col=ext.end.column,
         name_line=loc.line,
         name_col=loc.column,
         spelling=cursor.spelling,
         is_definition=is_def,
         storage_class=sc_name,
      ))

   return out
