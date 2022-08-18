import re
from enum import Enum, auto
from cmake_var import cmake_var, build_cmake_var

class row_type(Enum):
   EMPTY = auto()
   SLASH_COMMENT = auto()
   POUND_COMMENT = auto()
   VARIABLE = auto()

class cmake_row:
   """
   simple utility class that abstracts away some details from the parsing
   process from a single CMakeCache.txt row.
   """
   regex_expression = re.compile(r"^([A-Za-z_0-9]+?):([A-Za-z]+?)=(.*)$")

   def __init__(self, raw_row: str, row_number: int, comment: str = ""):
      """
      strips variable and assigns its correct type by matching the regex
      and verifying if it's a comment.
      """
      row = raw_row.strip()
      variable_match = re.match(self.regex_expression, row)
      self.val: str | cmake_var | None = None
      self.name: str = ""
      self._cmake_type: str # used to serialize back the value
      self.row_number: int = row_number
      self.comment: str = comment

      if not len(row):
         self.row_type = row_type.EMPTY
         self.val = ""

      elif self.is_slash_comment(row):
         self.row_type = row_type.SLASH_COMMENT
         self.val = row[2:]

      elif self.is_pound_comment(row):
         self.row_type = row_type.POUND_COMMENT
         self.val = row[1:]

      elif variable_match: # guarantees that the regex has exactly 3 matches
         groups = variable_match.groups()
         self.name = groups[0]
         self.row_type = row_type.VARIABLE
         val = groups[2]
         self._cmake_type = groups[1]
         self.val = build_cmake_var(self._cmake_type, val)

      else:
         raise ValueError("Could not parse variable")

   def is_comment(self, row: str) -> bool:
      "checks if string is either // comment or # comment"
      return self.is_slash_comment(row) or self.is_pound_comment(row)

   def is_slash_comment(self, row: str) -> bool:
      return row[:2] == "//"

   def is_pound_comment(self, row: str) -> bool:
      return row[0] == "#"

   def serialize(self) -> str:
      if self.row_type == row_type.VARIABLE:
         return self.name + ":" + self._cmake_type + "=" + self.val.serialize()
      elif self.row_type == row_type.POUND_COMMENT:
         return "#" + str(self.val)
      elif self.row_type == row_type.SLASH_COMMENT:
         return "//" + str(self.val)
      elif self.row_type == row_type.EMPTY:
         return ""
      else:
         raise ValueError("Row does not have a defined type")

   def get_val(self) -> str:
      if self.row_type == row_type.VARIABLE:
         return self.val.serialize()
      else:
         return str(self.val)
