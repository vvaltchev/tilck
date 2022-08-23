from cmake_row import cmake_row, row_type
from typing import Deque, List
from collections import deque

def read_file(filepath):
   with open(filepath, 'r') as f:
      return f.readlines()

def parse_rows(file_lines: List[str]) -> List[cmake_row]:
   cmake_rows: List[cmake_row] = []
   comment_list: Deque[str] = deque()
   for i, line in enumerate(file_lines):
      current_row = cmake_row(line, i)
      cmake_rows.append(current_row)
      if current_row.row_type == row_type.VARIABLE:
         j = i-1
         while j >= 0 and cmake_rows[j].row_type == row_type.SLASH_COMMENT:
            comment_list.appendleft(cmake_rows[j].get_val())
            j -= 1
         cmake_rows[i] = cmake_row(line, j, list(comment_list))
         comment_list.clear()
         """
         In this case we need a new cmake_row to handle all the new
         information provided by the metadata, so every cmake_row will be
         constructed twice. The process could also be extracted to other
         functions, but this respects the seperation of concerns.
         """
   return cmake_rows

def write_file(filepath: str, rows: List[cmake_row]):
   with open(filepath, 'w') as f:
      for row in rows:
         f.write(row.serialize() + '\n')
