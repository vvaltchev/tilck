import re
from typing import Match, Optional
from enum import Enum, auto

class row_type(Enum):
   EMPTY = auto()
   SLASH_COMMENT = auto()
   POUND_COMMENT = auto()
   VARIABLE = auto()

class configurator_type(Enum):
   BOOL = auto()
   FILEPATH = auto()
   STRING = auto()
   INTERNAL = auto()

class cmake_row:
   """
   simple utility class that abstracts away some details from the parsing
   process from a single CMakeCache.txt row.
   """
   regex_expression = re.compile(r"^([A-Za-z_0-9]+?):([A-Za-z]+?)=(.*)$")

   NORMALIZED_BOOLS = {
      "ON" : True,
      "OFF": False,
      "1" : True,
      "0" : False,
      "TRUE": True,
      "FALSE": False,
   }

   cmake_type_to_conf_type  = {
      "BOOL": configurator_type.BOOL,
      "FILEPATH": configurator_type.FILEPATH,
      "STRING": configurator_type.STRING,
      "INTERNAL": configurator_type.INTERNAL,
   }

   def __init__(self, raw_row: str, row_number: int, comment: str = ""):
      """
      strips variable and assigns its correct type by matching the regex
      and verifying if it's a comment.
      """
      row  = raw_row.strip()
      variable_match: Optional[Match[str]]  = re.match(self.regex_expression, row)
      self.row_type: row_type | None = None
      self.val: str | bool | None = None
      self.name: str = ""
      self._cmake_type_str: str # used to serialize back the value
      self.configurator_type: configurator_type
      self.row_number: int = row_number
      self.comment: str = comment

      if not len(row):
         self.row_type = row_type.EMPTY

      elif self.is_slash_comment(row):
         self.row_type = row_type.SLASH_COMMENT
         self.val = row[2:]

      elif self.is_pound_comment(row):
         self.row_type = row_type.POUND_COMMENT
         self.val = row[1:]

      elif variable_match: # this guarantees that the regex has exactly 3 matches
         groups = variable_match.groups()
         self.name = groups[0]
         self.row_type = row_type.VARIABLE
         self.val = groups[2]
         self._cmake_type_str = groups[1]
         self.configurator_type = \
            self.cmake_type_to_conf_type[self._cmake_type_str]
         if self.configurator_type == configurator_type.BOOL:
            self.val = self.normalize_bool(self.val)
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
      serialized_val: str
      if self.row_type == row_type.VARIABLE:
         if self.configurator_type == configurator_type.BOOL:
            serialized_val = "ON" if self.val else "OFF"
         else:
            serialized_val = str(self.val)
         return self.name + ":" + self._cmake_type_str + "="  + serialized_val
      elif self.row_type == row_type.POUND_COMMENT:
         return "#" + self.val
      elif self.row_type == row_type.SLASH_COMMENT:
         return "//" + self.val
      return "" # in case of empty string

   def normalize_bool(self, val: str) -> bool:
      return self.NORMALIZED_BOOLS[val]
