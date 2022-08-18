from cmake_row import cmake_row, row_type
from typing import List

def read_file(filepath):
   with open(filepath, 'r') as f:
      return f.readlines()

def parse_rows(file_lines: List[str]) -> List[cmake_row]:
   cmake_rows: List[cmake_row] =  []
   comment = ""
   for i, line in enumerate(file_lines):
      current_row = cmake_row(line, i)
      cmake_rows.append(current_row)
      if current_row.row_type == row_type.VARIABLE:
         j = i-1
         while j >= 0 and cmake_rows[j].row_type == row_type.SLASH_COMMENT:
            comment = cmake_rows[j].get_val() + comment
            j -= 1
         current_row.comment = comment
         comment = ""

   return cmake_rows

def write_file(filepath: str, rows: List[cmake_row]):
   with open(filepath, 'w') as f:
      for row in rows:
         f.write(row.serialize() + '\n')
