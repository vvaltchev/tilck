"""
basic idea here is just having a special variable that deals with i/o
and validation, while this just communicates with TUI.
"""
from cmake_string import cmake_string


class cmake_variable:
   BOOL_MAPPINGS = {
      "ON" : True,
      "OFF": False,
      "0" : True,
      "1" : False,
      "TRUE": True,
      "FALSE": False,
   }

   def __init__(self, line: str, comment: str, row_number: int) -> None:
      self.comment: str = comment
      self.row_number: row_number = row_number
      s = cmake_string(line)
      self.name, self.type, self.val =  s.parse_variable().groups()

   def is_valid(self, user_input: str) -> bool:
      """
      TODO: this might need to be extened in the future.
      """
      if self.type == "BOOL":
         return user_input.upper() in self.BOOL_MAPPINGS
      elif self.type == "INTERNAL":
         return False
      return True


   def serialize_line(self) -> str:
      "returns string in the correct format to be dumped to file"
      return self.name + ":" + self.type + "=" + self.val
