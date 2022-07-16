from cmake_variable import cmake_variable
from cmake_string import cmake_string

class cmake_parser:
   def parse(self, file_path: str) -> list[cmake_variable]:
      """
      this will communicate with the gui business logic, and pass to it
      variables ready to be displayed
      """
      comment: str = ""
      all_vars: list[cmake_variable] = []
      self.file_lines = []
      with open(file_path) as f:
         self.file_lines = f.readlines()

      for i, line in enumerate(self.file_lines):
         parsed_string: cmake_string = cmake_string(line)
         if parsed_string.is_variable():
            j = i
            while j >=0 and cmake_string(self.file_lines[j]).is_comment():
               comment = cmake_string(self.file_lines[j]).user_comment() +  comment
               j -= 1
            all_vars.append(cmake_variable(line, comment, i))

      return all_vars

   def write(self, new_vars: list[cmake_variable], file_path: str) -> None:
      """
      this will take the modified input from the gui and write it to file, then
      the gui will re-read  and display the file contents in order to have an
      updated version. Since we assume new data will come from the GUI,
      here we assume that the file is already parsed.
      """
      for var in new_vars:
         self.file_lines[var.row_number] = var.serialize_line()

      with open(file_path, "w+") as f:
         f.writelines(self.file_lines)
