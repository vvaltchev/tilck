import re

class cmake_string(str):
   """
   simple utility string that abstracts away some details from the parsing
   process
   """
   regex_expression =  r"^([A-Za-z_0-9]+?):([A-Za-z]+?)=(.*)$"

   def user_comment(self) -> str:
      """
      if a line starts with // we assume it's a comment, and we strip away the
      newline that is added by CMake
      """
      return self[2:].rstrip()

   def is_comment(self) -> bool:
      return self[:2] == "//" or self[0] == "#"

   def is_empty(self) -> bool:
      """
      lenght assumption is safe in this case because of the CMakeCache format
      A comment has at least lenght 2, while a variable has always a name and
      an explicit type
      """
      return len(self) <= 1

   def parse_variable(self):
      """
      match has three groups, the first is variable name, the second is its
      type and the third is its value
      """

      # rstrip is needed for 3rd group
      m = re.match(self.regex_expression, self.rstrip())
      return m

   def is_variable(self) -> bool:

      m = self.parse_variable()
      return (m and len(m.groups()) >= 3)
