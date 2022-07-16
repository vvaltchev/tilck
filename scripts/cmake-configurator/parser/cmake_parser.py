from cmake_row import cmake_row
from cmake_variable import cmake_variable
from typing import List

class cmake_parser:
   def read_file(self, abs_file_path: str) -> None:
      "reads file lines and inserts them into file_lines"
      self.file_lines: List[str] = []
      with open(abs_file_path) as f:
         self.file_lines = f.readlines()

   def lines_to_vars(self, file_lines: List[str]) -> List[cmake_variable]:
      """
      this will communicate with the gui business logic, and pass to it
      variables ready to be displayed
      """
      self.file_lines: List[str] = file_lines
      cmake_rows :List[cmake_row] =  []
      cmake_vars :List[cmake_variable] = []

      for i, line in enumerate(self.file_lines):
         cmake_rows.append(cmake_row(line, i))

      for i, row in enumerate(cmake_rows):
         if row.type == cmake_row.VARIABLE:
            comment: str = ""
            j = i-1
            while j >= 0 and cmake_rows[j].type == cmake_row.SLASH_COMMENT:
               comment = cmake_rows[j].val + comment
               j -=1
            cmake_vars.append(cmake_variable(row, comment))
      return cmake_vars

   def vars_to_lines(self, cmake_vars: List[cmake_variable]) -> List[str]:
      """
      this will take the modified input from the gui and write it to file, then
      the gui will re-read  and display the file contents in order to have an
      updated version. Since we assume new data will come from the GUI,
      here we assume that the file is already parsed.
      """
      for var in cmake_vars:
         if var.row.type == cmake_row.VARIABLE:
            self.file_lines[var.row.row_number] = var.row.serialize()
      return self.file_lines

   def write_to_file(self, abs_file_path: str) -> None:
      with open(abs_file_path, "w+") as f:
         f.writelines(self.file_lines)
